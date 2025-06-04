from flask import Flask, send_from_directory, Response, jsonify, request
import cv2, dlib, numpy as np, os, json, threading, time
from flask import Flask, send_from_directory, Response, jsonify, request, redirect, render_template_string, session, url_for
from flask import render_template
import requests
# 初始化 Flask 应用
app = Flask(__name__)

# 全局列表，用于存储当前识别到的人脸名称
recognized_names = []
DEEPSEEK_API_KEY = 'sk-f84bc88200bd421f933d9bf1d3da4627'
DEEPSEEK_API_URL = 'https://api.deepseek.com/v1/chat/completions'

# ========== 模型加载（人脸检测、关键点定位、特征提取） ==========
# 确保必要的模型文件存在
assert os.path.exists("shape_predictor_68_face_landmarks.dat"), "缺少关键点定位模型文件"
assert os.path.exists("dlib_face_recognition_resnet_model_v1.dat"), "缺少人脸识别模型文件"

# 加载 dlib 模型用于人脸检测、68 个关键点定位和人脸特征提取
detector = dlib.get_frontal_face_detector()  # 人脸检测器
predictor = dlib.shape_predictor("shape_predictor_68_face_landmarks.dat")  # 68 点关键点定位
facerec = dlib.face_recognition_model_v1("dlib_face_recognition_resnet_model_v1.dat")  # 提取 128 维人脸特征向量

# ========== 加载已知人脸图像并提取特征 ==========
def load_known_faces(folder_path):
    """
    从指定文件夹加载已知人脸图像并提取特征向量。

    参数:
        folder_path (str): 包含已知人脸图像的文件夹路径。

    返回:
        tuple: 人脸特征向量列表和对应的标签（人名）。
    """
    known_faces = []
    known_labels = []
    for file_name in os.listdir(folder_path):
        if file_name.endswith((".jpg", ".png")):  # 处理 jpg 和 png 格式图像
            image_path = os.path.join(folder_path, file_name)
            image = cv2.imread(image_path)
            gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)  # 转换为灰度图
            faces = detector(gray)
            if faces:  # 仅处理检测到人脸的图像
                shape = predictor(image, faces[0])  # 获取第一个人脸的 68 个关键点
                descriptor = np.array(facerec.compute_face_descriptor(image, shape))  # 提取人脸特征向量
                known_faces.append(descriptor)
                known_labels.append(file_name.split('.')[0])  # 使用文件名（去掉扩展名）作为标签
    return known_faces, known_labels

# 从 known_faces 目录加载已知人脸
known_faces, known_labels = load_known_faces("known_faces")

# ========== 视频流类：异步帧捕获 ==========
class VideoStream:
    """
    通过后台线程异步从视频源捕获帧。

    属性:
        cap (cv2.VideoCapture): OpenCV 视频捕获对象。
        frame (ndarray): 最新捕获的帧。
        running (bool): 控制后台线程的标志。
    """
    def __init__(self, src):
        self.cap = cv2.VideoCapture(src)  # 从指定视频源初始化捕获
        self.frame = None
        self.running = True
        # 启动后台线程以持续更新帧
        thread = threading.Thread(target=self.update, daemon=True)
        thread.start()

    def update(self):
        """在后台线程中持续捕获视频帧。"""
        while self.running:
            if self.cap.isOpened():
                ret, frame = self.cap.read()
                if ret:
                    self.frame = frame

    def get_frame(self):
        """返回最新捕获的帧。"""
        return self.frame

    def stop(self):
        """停止视频流并释放资源。"""
        self.running = False
        self.cap.release()

# 从 ESP32 摄像头或其他视频源初始化视频流
video_stream = VideoStream("http://192.168.137.127:81/")  # ESP32 视频流地址
# ==========想deapseak发送数据，获取建议 ==========
def generate_advice(sensor_data):
    prompt = (
        f"请根据以下环境传感器数据，判断当前环境的舒适度，并给出详细个性化建议，我的地址在上海，也考虑上海今天的情况，家里右两个月大小的婴儿，快速回答：\n"
        f"- 温度：{sensor_data['temperature']}℃\n"
        f"- 湿度：{sensor_data['humidity']}%\n"
        f"- 光照强度：{sensor_data['light']} lux\n"
        f"- 二氧化碳浓度：{sensor_data['co2']} ppm\n\n"
        "要求：给出生活化建议，比如是否开窗、开空调、是否适合运动注意事项，出门注意事项，未来天气等。"
    )

    headers = {
        "Authorization": f"Bearer {DEEPSEEK_API_KEY}",
        "Content-Type": "application/json"
    }

    data = {
        "model": "deepseek-chat",
        "messages": [{"role": "user", "content": prompt}],
        "temperature": 0.7
    }

    response = requests.post(DEEPSEEK_API_URL, headers=headers, json=data)
    response.raise_for_status()
    result = response.json()
    return result["choices"][0]["message"]["content"]


# ========== 视频帧生成器（MJPEG 流） ==========
def generate_video_feed():
    """
    生成带有人脸检测和识别标注的 MJPEG 视频流。

    产出:
        bytes: 包含 MJPEG 帧数据和边界头。
    """
    global recognized_names
    frame_count = 0
    skip_interval = 5  # 每 5 帧执行一次人脸识别以降低 CPU 负载
    last_time = time.time()
    fps_limit = 10  # 限制处理帧率为 10 FPS 以优化性能

    while True:
        frame = video_stream.get_frame()
        if frame is None:
            continue

        # 控制帧率，避免过高处理频率
        if time.time() - last_time < 1.0 / fps_limit:
            continue
        last_time = time.time()

        frame_count += 1
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)  # 转换为灰度图以进行检测

        if frame_count % skip_interval == 0:  # 每隔 skip_interval 帧进行人脸识别
            faces = detector(gray)
            names = []
            face_positions = []  # 记录人脸框位置

            for face in faces:
                shape = predictor(frame, face)  # 获取人脸关键点
                descriptor = np.array(facerec.compute_face_descriptor(frame, shape))  # 提取特征向量
                label = "stranger"  # 默认标签为陌生人
                if known_faces:
                    # 计算当前人脸与已知人脸的欧几里得距离
                    distances = [np.linalg.norm(descriptor - kf) for kf in known_faces]
                    min_dist = min(distances)
                    if min_dist < 0.6:  # 阈值 0.6 判断是否为已知人脸
                        label = known_labels[distances.index(min_dist)]
                names.append(label)
                face_positions.append((face.left(), face.top(), face.right(), face.bottom()))  # 保存人脸框坐标

            recognized_names = names  # 更新全局人脸名称列表
        else:
            # 非识别帧，复用上一次的人脸位置和名称
            faces = detector(gray)
            face_positions = [(face.left(), face.top(), face.right(), face.bottom()) for face in faces]
            names = recognized_names

        # 绘制人脸框和名称标签
        for (left, top, right, bottom), name in zip(face_positions, names):
            cv2.rectangle(frame, (left, top), (right, bottom), (0, 255, 0), 2)  # 绿色矩形框
            cv2.putText(frame, name, (left, top - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)  # 名称标签

        # 编码帧为 JPEG 格式
        ret, jpeg = cv2.imencode('.jpg', frame)
        if not ret:
            continue
        # 产出 MJPEG 流帧
        yield (b'--frame\r\nContent-Type: image/jpeg\r\n\r\n' + jpeg.tobytes() + b'\r\n')


app.secret_key = 'your_secret_key'  # 设置会话密钥，用于管理登录状态


# 简单用户数据（可替换为数据库验证）
users = {
    "admin": "123456",
    "user1": "password"
}

# ========== Flask 路由 ==========

# ========== 登录页面 ==========
@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get("username")
        password = request.form.get("password")
        if users.get(username) == password:
            session['username'] = username  # 登录成功，保存会话
            return redirect('/')
        else:
            return render_template('login.html', message="用户名或密码错误")
    return render_template('login.html', message="")

# ========== 登出 ==========
@app.route('/logout')
def logout():
    session.pop('username', None)
    return redirect('/login')
@app.route('/')
def serve_index():
    if 'username' not in session:
        return redirect('/login')
    return send_from_directory('public', 'index.html')
@app.route("/advice", methods=["GET"])
def get_advice():
    try:
        if not latest_data or not isinstance(latest_data, dict):
            return jsonify({
                "advice": "❌ 当前无法获取有效环境数据，请确认设备是否在线并正常上报。"
            })

        # 从最新数据中提取字段并规范字段名（大小写统一）
        sensor_data = {
            "temperature": latest_data.get("Temperature"),
            "humidity": latest_data.get("Humidity"),
            "light": latest_data.get("lux"),
            "co2": latest_data.get("co2")
        }

        # 检查是否所有字段都有效
        if any(v is None for v in sensor_data.values()):
            return jsonify({
                "advice": "⚠️ 当前传感器数据不完整，建议检查各模块状态。"
            })

        # 调用你原来的 generate_advice() 函数
        advice = generate_advice(sensor_data)
        return jsonify({"advice": advice})
    except Exception as e:
        return jsonify({
            "error": str(e),
            "message": "❌ 获取建议时发生错误"
        }), 500


@app.route('/<path:path>')
def serve_static(path):
    """
    提供静态资源（JS、CSS 等）。

    参数:
        path (str): 请求的静态资源路径。
    """
    if path.endswith('.js'):  # 特殊处理 JavaScript 文件
        return send_from_directory('.', path)
    return send_from_directory('public', path)  # 从 public 目录提供其他静态资源

@app.route('/api/faces')
def recognized_faces_api():
    """返回当前识别到的人脸名称和数量。"""
    try:
        return jsonify({
            "names": recognized_names or [],
            "count": len(recognized_names)
        })
    except Exception as e:
        return jsonify({
            "error": str(e),
            "message": "无法获取人脸识别数据"
        }), 500

# 全局变量，保存最新的传感器数据（如温度、湿度）
latest_data = {}
#  这段代码是一个用 Flask 编写的后端 API，包含两个路由接口：一个用于接收 ESP32 等设备发送的传感器数据（POST），一个用于向前端提供最新的数据（GET）.
@app.route('/api/data', methods=['POST'])
def receive_data():
    global latest_data
    try:
        if request.is_json:
            parsed_data = request.get_json()
        elif request.form.get("data"):
            parsed_data = json.loads(request.form.get("data"))
        else:
            parsed_data = json.loads(request.data.decode("utf-8"))

        print("收到的数据原文：", parsed_data)

        # ✅ 改这里
        if isinstance(parsed_data, dict) and "data" in parsed_data:
            latest_data = parsed_data["data"]  # ✅ 去掉外层 "data"
            print("✔ 已解析并保存：", latest_data)
            return jsonify({"status": "success"}), 200
        else:
            return jsonify({"status": "ignored", "reason": "unrecognized structure"}), 200
    except Exception as e:
        print("❌ 解析失败：", e)
        return jsonify({"status": "error", "error": str(e)}), 400


@app.route('/api/data', methods=['GET'])
def get_data():
    """返回最新的传感器数据，若无数据则返回默认值。"""
    try:
        return jsonify(latest_data or {
            "Temperature": 26,
            "Humidity": 0,
            "lux": 0,
            "co2": 0,
            "mq2": 0
        })
    except Exception as e:
        return jsonify({
            "error": str(e),
            "message": "无法获取环境数据"
        }), 500

@app.route('/video_feed/bedroom')
def video_feed():
    """返回带有人脸识别的实时 MJPEG 视频流。"""
    return Response(generate_video_feed(), mimetype='multipart/x-mixed-replace; boundary=frame')

# ========== 启动服务 ==========
if __name__ == '__main__':
    try:
        # 启动 Flask 服务器，监听所有网络接口，端口 5000
        app.run(host='0.0.0.0', port=5000)
    finally:
        # 确保程序退出时释放视频流资源
        video_stream.stop()

