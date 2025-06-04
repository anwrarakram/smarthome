#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "anwar";      // Wi-Fi 名称
const char* password = "12345678"; // Wi-Fi 密码
const char* serverUrl = "http://192.168.137.1:5000/api/data"; // 替换为你的服务器地址

void setup() {
  Serial.begin(115200);
  
  // 连接WiFi
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // 设置串口接收数据处理
  Serial.println("ESP-01 is ready to receive data.");
}

void loop() {
  // 如果串口有数据，处理数据并发送到服务器
  if (Serial.available()) {
    String data = Serial.readString();  // 读取串口数据
    
    // 打印接收到的数据
    Serial.println("Received data: " + data);
    
    // 发送数据到远程服务器
    sendDataToServer(data);
  }
}

void sendDataToServer(String data) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // 解析字符串为 JSON 对象
    StaticJsonDocument<200> sensorDoc;
    DeserializationError error = deserializeJson(sensorDoc, data);
    if (error) {
      Serial.println("❌ JSON 解析失败！");
      return;
    }

    // 包装为 { "data": sensor_data }
    StaticJsonDocument<256> jsonDoc;
    jsonDoc["data"] = sensorDoc;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    Serial.println("🚀 发送 JSON: " + jsonString);

    int httpResponseCode = http.POST(jsonString);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("HTTP Response code: " + String(httpResponseCode));
      Serial.println("Response: " + response);
    } else {
      Serial.println("Error on sending POST: " + String(httpResponseCode));
    }

    http.end();
  } else {
    Serial.println("WiFi Disconnected");
  }
}


