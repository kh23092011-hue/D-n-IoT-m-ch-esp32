#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>

#define WIFI_SSID "H2SO4+Cu=?"
#define WIFI_PASSWORD "CuSO4+H2"

// Link gốc Realtime Database (không cần dấu / ở cuối)
#define FIREBASE_URL "https://dong-dat-esp32-default-rtdb.asia-southeast1.firebasedatabase.app/SensorData"

const int MQ2_AnalogPin = 34;
const int FlamePin = 15;
const int MPU_addr = 0x68;

void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Dang ket noi WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nDa ket noi WiFi thanh cong!");

  // Khởi tạo MPU6050
  Wire.begin(21, 22); 
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x6B);   
  Wire.write(0);      
  Wire.endTransmission(true);

  pinMode(FlamePin, INPUT);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    // Đọc dữ liệu MPU6050
    Wire.beginTransmission(MPU_addr);
    Wire.write(0x3F);   
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_addr, 2, true);
    int16_t AcZ = Wire.read() << 8 | Wire.read();

    int gasValue = analogRead(MQ2_AnalogPin);
    bool isFire = !digitalRead(FlamePin); 

    // Tạo chuỗi JSON gửi lên Firebase
    String jsonPayload = "{";
    jsonPayload += "\"VibrationZ\":" + String(AcZ) + ",";
    jsonPayload += "\"GasLevel\":" + String(gasValue) + ",";
    jsonPayload += "\"FireAlert\":" + String(isFire ? 1 : 0);
    jsonPayload += "}";

    HTTPClient http;
    // Dùng phương pháp PATCH để cập nhật các trường trong SensorData mà không làm mất nhánh khác
    http.begin(FIREBASE_URL + String(".json"));
    http.addHeader("Content-Type", "application/json");
    
    int httpResponseCode = http.PATCH(jsonPayload);

    if (httpResponseCode > 0) {
      Serial.printf("-> Day du lieu thanh cong! HTTP Code: %d\n", httpResponseCode);
    } else {
      Serial.printf("-> Loi gui HTTP: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    
    http.end();
    Serial.printf("AcZ: %d | Gas: %d | Fire: %d\n", AcZ, gasValue, isFire);
  }
  
  delay(1500);
}
