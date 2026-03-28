#include <Arduino.h>
#include <TinyGPS++.h>
#include <MPU6050.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- 用户配置区 ---
const char* ssid     = "YH的iPhone"; 
const char* password = "@xzc666666";
const char* uid      = "c3ace5d30eaaaf913aec1965b85384a1"; // 截图左上角那个
const char* topic    = "gps001";        // 你创建的主题

// --- 硬件定义 ---
#define GPS_RX 4
#define GPS_TX 5
#define I2C_SDA 8
#define I2C_SCL 9

// --- 常量定义 ---
const char* mqtt_server = "bemfa.com";
const int mqtt_port = 9501;

// --- 对象实例化 ---
TinyGPSPlus gps;
MPU6050 mpu;
HardwareSerial gpsSerial(1);
WiFiClient espClient;
PubSubClient client(espClient);

// --- 函数声明 ---
void setupWiFi();
void reconnect();
void uploadToBemfa(float lat, float lng, float speed, float alt, float gForce, float tilt, float gyroX, float gyroY, float gyroZ);

void setup() {
    Serial.begin(115200);
    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
    Wire.begin(I2C_SDA, I2C_SCL);

    Serial.println("\n======================================");
    Serial.println("      SKILOGGER - 智能雪盔系统启动      ");
    Serial.println("======================================");

    // 初始化 MPU6050
    mpu.initialize();
    if (mpu.testConnection()) {
        Serial.println("[OK] 惯性传感器 MPU6050 已连接");
    } else {
        Serial.println("[ERROR] MPU6050 连接失败");
    }

    // 初始化 WiFi 和 MQTT
    setupWiFi();
    client.setServer(mqtt_server, mqtt_port);
}

void loop() {
    // 1. 处理 GPS 信号 (必须在 loop 里高频执行)
    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    // 2. 检查 MQTT 连接状态
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    // 3. 定时处理数据 (改成每 200 毫秒获取并上传一次)
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 200) {
        lastUpdate = millis();

        // 读取 MPU6050
        int16_t ax, ay, az, gx, gy, gz;
        mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        
        // 计算加速度和倾角
        float accG = sqrt(sq(ax/16384.0) + sq(ay/16384.0) + sq(az/16384.0));
        float tilt = atan2(ax, az) * 180 / PI;

        // 计算三轴角速度
        float gyroX = gx / 131.0;
        float gyroY = gy / 131.0;
        float gyroZ = gz / 131.0;

        // 串口本地打印报告
        Serial.println("\n>>>>>> 实时运行状态报告 <<<<<<");
        if (gps.location.isValid()) {
            Serial.printf("【位置】 %.6f, %.6f | 海拔: %.1f m | 时速: %.1f km/h\n", gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.kmph());
        } else {
            Serial.println("【位置】 搜星中...");
        }
        
        Serial.printf("【姿态】 G力: %.2f g | 倾角: %.1f°\n", accG, tilt);
        Serial.printf("【角速度】 X: %.1f °/s | Y: %.1f °/s | Z: %.1f °/s\n", gyroX, gyroY, gyroZ);

        // 4. 取消 5 秒计数器，直接每次都上传！
        uploadToBemfa(
            gps.location.lat(), 
            gps.location.lng(), 
            gps.speed.kmph(), 
            gps.altitude.meters(), 
            accG, 
            tilt,
            gyroX,
            gyroY,
            gyroZ
        );
        Serial.println("------------------------------------");
    }
}

// --- 连接 WiFi ---
void setupWiFi() {
    delay(10);
    Serial.print("\n[WiFi] 正在连接 ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] 连接成功！IP: " + WiFi.localIP().toString());
}

// --- MQTT 重连 ---
void reconnect() {
    while (!client.connected()) {
        Serial.print("[MQTT] 正在连接巴法云...");
        if (client.connect(uid)) { // 使用 UID 作为 ClientID
            Serial.println("已连接");
        } else {
            Serial.print("失败, rc=");
            Serial.print(client.state());
            Serial.println(" 5秒后重试");
            delay(5000);
        }
    }
}

// --- 上传数据到巴法云 ---
void uploadToBemfa(float lat, float lng, float speed, float alt, float gForce, float tilt, float gyroX, float gyroY, float gyroZ) {
    StaticJsonDocument<256> doc; 
    
    // 如果 GPS 无效，传 
    doc["lat"] = lat != 0 ? lat : 0.0;
    doc["lng"] = lng != 0 ? lng : 0.0;
    doc["spd"] = speed;
    doc["alt"] = alt; // 海拔数据在此处打包上传
    doc["g"]   = gForce;
    doc["tlt"] = tilt;
    doc["gyx"] = gyroX;
    doc["gyy"] = gyroY;
    doc["gyz"] = gyroZ;

    char buffer[256];
    serializeJson(doc, buffer);

    if (client.publish(topic, buffer)) {
        Serial.println("[云端] 数据上传成功!");
    } else {
        Serial.println("[云端] 数据上传失败!");
    }
}