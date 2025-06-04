#include "esp_camera.h"
#include <WiFi.h>

// 配置WiFi网络
const char* ssid = "anwar";        // Wi-Fi 名称
const char* password = "12345678"; // Wi-Fi 密码

// 摄像头引脚配置
#define PWDN_GPIO_NUM     32  // 电源控制引脚
#define RESET_GPIO_NUM    -1  // 重置引脚，未使用
#define XCLK_GPIO_NUM      0  // 时钟引脚
#define SIOD_GPIO_NUM     26  // I2C SDA
#define SIOC_GPIO_NUM     27  // I2C SCL

#define Y9_GPIO_NUM       35  // 摄像头数据引脚 Y9
#define Y8_GPIO_NUM       34  // 摄像头数据引脚 Y8
#define Y7_GPIO_NUM       39  // 摄像头数据引脚 Y7
#define Y6_GPIO_NUM       36  // 摄像头数据引脚 Y6
#define Y5_GPIO_NUM       21  // 摄像头数据引脚 Y5
#define Y4_GPIO_NUM       19  // 摄像头数据引脚 Y4
#define Y3_GPIO_NUM       18  // 摄像头数据引脚 Y3
#define Y2_GPIO_NUM        5  // 摄像头数据引脚 Y2
#define VSYNC_GPIO_NUM    25  // 垂直同步信号引脚
#define HREF_GPIO_NUM     23  // 水平同步信号引脚
#define PCLK_GPIO_NUM     22  // 像素时钟引脚

WiFiServer server(81);  // Web服务器监听端口 40

// 启动摄像头服务器
void startCameraServer() {
  server.begin();
}

void setup() {
  Serial.begin(115200);  // 初始化串口调试输出

  // 摄像头配置结构体
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;   // LEDC通道
  config.ledc_timer   = LEDC_TIMER_0;     // LEDC计时器
  config.pin_d0       = Y2_GPIO_NUM;      // 数据引脚
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;    // 时钟引脚
  config.pin_pclk     = PCLK_GPIO_NUM;    // 像素时钟引脚
  config.pin_vsync    = VSYNC_GPIO_NUM;   // 垂直同步信号引脚
  config.pin_href     = HREF_GPIO_NUM;    // 水平同步信号引脚
  config.pin_sscb_sda = SIOD_GPIO_NUM;    // I2C SDA
  config.pin_sscb_scl = SIOC_GPIO_NUM;    // I2C SCL
  config.pin_pwdn     = PWDN_GPIO_NUM;    // 电源控制引脚
  config.pin_reset    = RESET_GPIO_NUM;   // 重置引脚
  config.xclk_freq_hz = 20000000;         // 时钟频率
  config.pixel_format = PIXFORMAT_JPEG;   // 图像格式

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QQVGA;   // 降低分辨率至160x120，进一步减小每帧大小
    config.jpeg_quality = 12;               // 降低JPEG质量，进一步减少文件大小
    config.fb_count = 1;                    // 使用一个帧缓存，减少内存使用
  } else {
    config.frame_size = FRAMESIZE_QQVGA;   // 降低分辨率至160x120
    config.jpeg_quality = 25;               // 更低的JPEG质量，减小图像尺寸
    config.fb_count = 1;                    // 使用一个帧缓存
  }

  // 摄像头初始化
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);  // 如果初始化失败，输出错误
    return;
  }

  // 连接Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);  // 延迟300毫秒，避免过于频繁地查询
    Serial.print(".");  // 打印连接进度
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("Stream Ready! URL: http://");
  Serial.print(WiFi.localIP());  // 输出连接的IP地址
  Serial.println(":81");

  startCameraServer();  // 启动摄像头服务器
}

void loop() {
  // 等待客户端连接
  WiFiClient client = server.available();
  if (client) {
    String req = client.readStringUntil('\r');  // 读取客户端请求
    Serial.println(req);
    client.flush();

    if (req.indexOf("GET /") != -1) {  // 检测客户端请求
      String response = "HTTP/1.1 200 OK\r\n";  // HTTP响应头
      response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";  // 设置多部分流格式
      client.print(response);

      while (client.connected()) {  // 持续传输图像流
        camera_fb_t * fb = esp_camera_fb_get();  // 获取摄像头帧数据
        if (!fb) {
          Serial.println("Camera capture failed");  // 如果获取失败，输出错误信息
          continue;  // 继续下次获取
        }

        // 向客户端发送JPEG图像数据
        client.printf("--frame\r\n");
        client.printf("Content-Type: image/jpeg\r\n");
        client.printf("Content-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len);
        client.printf("\r\n");

        esp_camera_fb_return(fb);  // 释放帧缓存
        delay(20);  // 延迟20毫秒，避免过快发送数据
      }
    }
  }
}

