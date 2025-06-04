#include <Wire.h>
#include <U8g2lib.h>
#include <RTClib.h>
#include <DHT.h>
#include <BH1750.h>
#include <SPI.h>
#include <SD.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>

// DHT11 设置
#define DHTPIN 3
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// I2C 引脚定义
#define I2C0_SDA 25
#define I2C0_SCL 26
#define I2C1_SDA 8
#define I2C1_SCL 22

// 传感器引脚
#define MQ2_PIN 11
#define PIR_PIN 12
// SD 卡引脚定义
const int CS_PIN = 5;
const int MOSI_PIN = 0;
const int MISO_PIN = 2;
const int SCK_PIN = 4;

// I2C 总线
TwoWire I2C_BUS_0 = TwoWire(0);
TwoWire I2C_BUS_1 = TwoWire(1);

// 光照传感器
BH1750 lightMeter;
// Senseair S8 CO₂传感器请求帧
byte requestCO2[] = {0xFE, 0x44, 0x00, 0x08, 0x02, 0x9F, 0x25};
byte response[7];             // 存储响应
// RTC 时钟
RTC_DS3231 rtc;

// OLED 显示对象（使用 I2C_BUS_1）
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, I2C1_SCL, I2C1_SDA);

// 状态变量
float Humidity, Temperature, lux;
// CO₂ 模拟初始值
int co2 = 400;
bool oledOn = true;
// 控制显示逻辑的时间标志
unsigned long lastMotionTime = 0;
unsigned long lastActionTime = 0;
unsigned long previousMillis_esp01 = 0;  // 上次发送数据的时间
const long interval_esp01 = 5000;        // 5秒钟的时间间隔（单位：毫秒）
const unsigned long displayTimeout = 10000; // 10秒无动作关闭OLED
const unsigned long interval = 6UL * 1000UL;     // 每6秒保存一次数据

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(MQ2_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  delay(1000);

  // 初始化 I2C 总线
  I2C_BUS_0.begin(I2C0_SDA, I2C0_SCL, 100000);
  I2C_BUS_1.begin(I2C1_SDA, I2C1_SCL, 100000);

  // 初始化 BH1750
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &I2C_BUS_0);

  // 初始化 RTC
  if (!rtc.begin(&I2C_BUS_0)) {
    Serial.println("RTC 未找到！");
    while (1);
  }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // 仅首次上传时启用

  // 初始化 SD 卡
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
  if (!SD.begin(CS_PIN)) {
    Serial.println("⚠️ SD 卡初始化失败");
  } else {
    Serial.println("✅ SD 卡初始化成功");
  }

  // OLED 初始化（U8g2 使用 Wire，所以重定向）
  Wire = I2C_BUS_1;
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);

  // 欢迎画面
  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "OLED 測試");
  u8g2.drawStr(0, 24, "ESP32-H 雙 I2C");
  u8g2.drawStr(0, 38, "初始化完成");
  u8g2.sendBuffer();
  delay(2000);
}

void loop() {
  unsigned long currentMillis = millis();

  // 读取传感器数据
  lux = lightMeter.readLightLevel();
  DateTime now = rtc.now();
  Temperature = dht.readTemperature();
  Humidity = dht.readHumidity();
  int motion = digitalRead(PIR_PIN);
  int mq2Value = digitalRead(MQ2_PIN);
  
  if (isnan(Temperature) || isnan(Humidity)) {
    Serial.println("无法读取 DHT 传感器数据！");
    delay(5000);
    return;
  }

  if (motion == HIGH) {
    if (!oledOn) {
      u8g2.setPowerSave(0);
      oledOn = true;
    }
    lastMotionTime = currentMillis;
  } else if (oledOn && (currentMillis - lastMotionTime > displayTimeout)) {
    u8g2.setPowerSave(1);
    oledOn = false;
  }

  // 每5秒钟发送一次数据
  if (currentMillis - previousMillis_esp01 >= interval_esp01) {
    previousMillis_esp01 = currentMillis;  // 更新上次发送时间
    // 创建一个 JSON 对象并将传感器数据添加到该对象
    StaticJsonDocument<200> doc;  // 创建一个容量为 200 字节的 JSON 文档
    doc["Temperature"] = Temperature;
    doc["Humidity"] = Humidity;
    doc["lux"] = lux;
    doc["co2"] = co2;
    doc["mq2"] = mq2Value;

    // 将 JSON 数据转换为字符串并发送
    String jsonData;
    serializeJson(doc, jsonData);  // 将 JSON 对象转换为字符串
    Serial.println(jsonData);  // 发送数据到串口，ESP-01 会接收到并传输到服务器
  }

  // 每隔一段时间写入一次 SD 卡
  if (millis() - lastActionTime >= interval) {
    lastActionTime = millis();
    Serial.print("asasasa");
    WriteFile("/data.csv", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second(),
              (int)Temperature, (int)Humidity, (int)lux, 0, mq2Value == HIGH ? "报警" : "正常");
  }

  if (oledOn) {
    // 显示时间、日期、传感器数据
    char timeStr[20];
    sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

    char dateStr[40];
    sprintf(dateStr, "%02d-%02d-%04d %02d:%02d", now.day(), now.month(), now.year(), now.hour(), now.minute());

    char luxStr[32];
    sprintf(luxStr, "lux: %.1f lx", lux);

    char tempStr[20];
    sprintf(tempStr, "Temp: %.1f C", Temperature);

    char humiStr[20];
    sprintf(humiStr, "Humi: %.1f %%", Humidity);

    char mq2Str[20];
    sprintf(mq2Str, "Smoke: %s", mq2Value == HIGH ? "Yes" : "No");

    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, dateStr);  // 显示日期和时间
    u8g2.drawStr(0, 30, luxStr);
    u8g2.drawStr(0, 40, tempStr);
    u8g2.drawStr(0, 50, humiStr);
    u8g2.drawStr(0, 60, mq2Str);
    u8g2.sendBuffer();
  }

  delay(2000); // 主循环延时
}

// I2C 扫描函数
void scanI2C(TwoWire &wire) {
  byte error, address;
  int deviceCount = 0;

  Serial.println("掃描 I2C 總線...");

  for (address = 1; address < 127; address++) {
    wire.beginTransmission(address);
    error = wire.endTransmission();

    if (error == 0) {
      Serial.print("找到 I2C 設備，地址: 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      deviceCount++;
    }
  }

  if (deviceCount == 0) {
    Serial.println("未找到 I2C 設備");
  } else {
    Serial.print("共找到 ");
    Serial.print(deviceCount);
    Serial.println(" 個 I2C 設備");
  }
}

// 写入数据到文件
void WriteFile(const char* filename, int year, int month, int day, int hour, int minute, int second,
               int temp, int hum, int lux, int co2, const char* mq2Status) {
  File file = SD.open(filename, FILE_APPEND);  // ← 建议使用 FILE_APPEND
  if (file) {
    file.print(year); file.print("-"); file.print(month); file.print("-"); file.print(day);
    file.print(" "); file.print(hour); file.print(":"); file.print(minute); file.print(":"); file.print(second);
    file.print(" "); file.print(temp); file.print(" "); file.print(hum);
    file.print(" "); file.print(lux); file.print(" "); file.print(co2);
    file.print(" "); file.println(mq2Status);
    file.close();
    Serial.println("✅ 数据已写入 SD 卡");  // ← 增加提示
  } else {
    Serial.println("❌ 无法打开文件写入");
  }
}



