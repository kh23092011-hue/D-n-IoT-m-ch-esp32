#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <math.h>

// =========================================================================
// 1. CẤU HÌNH KẾT NỐI & ĐỊA CHỈ FIREBASE
// =========================================================================
#define WIFI_SSID "H2SO4+Cu=?"
#define WIFI_PASSWORD "CuSO4+H2"
#define FIREBASE_URL "https://dong-dat-esp32-default-rtdb.asia-southeast1.firebasedatabase.app/SensorData.json"

// =========================================================================
// 2. CHÂN CẮM & NGƯỠNG CẢNH BÁO (CÓ THỂ ĐIỀU CHỈNH)
// =========================================================================
const int MQ2_AnalogPin = 34;       // Chân ADC đọc tín hiệu analog MQ-2
const int FlamePin = 15;            // Chân digital đọc cảm biến lửa IR
const int MPU_addr = 0x68;          // Địa chỉ I2C của MPU6050

const float ACC_THRESHOLD_G = 0.25f; // Ngưỡng rung/nghiêng a_xy (đơn vị g)
const int MQ2_THRESHOLD = 1800;      // Ngưỡng cảnh báo khí gas MQ-2
const bool IR_ACTIVE_LOW = true;     // true nếu cảm biến lửa xuất LOW khi có lửa

const float DEADBAND_G = 0.03f;      // Lọc nhiễu nền nhỏ hơn 0.03g
const float ACTIVE_SEND_G = 0.05f;   // Mức a_xy tối thiểu để gửi định kỳ khi chưa báo động
const uint32_t SAMPLE_MS = 20;       // Chu kỳ đọc cảm biến (20ms = 50Hz)
const uint32_t SEND_MS = 500;        // Chu kỳ gửi tối thiểu lên Firebase (ms)
const uint32_t WIFI_TIMEOUT_MS = 15000;

// =========================================================================
// 3. BIẾN TRẠNG THÁI HỆ THỐNG
// =========================================================================
float ax0 = 0.0f, ay0 = 0.0f;        // Giá trị offset ban đầu khi đứng yên
float peakAccG = 0.0f;               // Giá trị dao động a_xy lớn nhất trong chu kỳ gửi
int peakMq2 = 0;                     // Giá trị MQ-2 lớn nhất trong chu kỳ gửi
bool fireDetected = false;           // Cờ ghi nhận có lửa trong chu kỳ gửi
bool lastAlarmState = false;         // Trạng thái cảnh báo lần gửi trước
uint32_t lastSampleMs = 0;
uint32_t lastSendMs = 0;

// Đọc 2 trục gia tốc X, Y từ MPU6050 (4 byte từ thanh ghi 0x3B) và đổi sang đơn vị g
bool readMpuXY(float &ax_g, float &ay_g) {
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;

  uint8_t count = Wire.requestFrom(MPU_addr, (uint8_t)4, (uint8_t)true);
  if (count < 4 || Wire.available() < 4) return false;

  int16_t rawX = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t rawY = (int16_t)((Wire.read() << 8) | Wire.read());

  ax_g = rawX / 16384.0f; // 16384 LSB/g ở thang +-2g mặc định
  ay_g = rawY / 16384.0f;
  return true;
}

// Hiệu chuẩn điểm 0 (ax0, ay0) lúc khởi động khi mô hình nằm yên
void calibrateMpuXY(int samples = 100) {
  float sx = 0.0f, sy = 0.0f;
  int ok = 0;
  for (int i = 0; i < samples; i++) {
    float x, y;
    if (readMpuXY(x, y)) {
      sx += x;
      sy += y;
      ok++;
    }
    delay(5);
  }
  if (ok > 0) {
    ax0 = sx / ok;
    ay0 = sy / ok;
  }
}

// Đọc 1 lượt tất cả cảm biến và cập nhật giá trị đỉnh
void sampleSensors() {
  float x_g, y_g;
  if (readMpuXY(x_g, y_g)) {
    float dx = x_g - ax0;
    float dy = y_g - ay0;
    float a_xy = sqrtf(dx * dx + dy * dy);
    if (a_xy < DEADBAND_G) a_xy = 0.0f;
    if (a_xy > peakAccG) peakAccG = a_xy;
  }

  int mqVal = analogRead(MQ2_AnalogPin);
  if (mqVal > peakMq2) peakMq2 = mqVal;

  int irRaw = digitalRead(FlamePin);
  bool irFire = IR_ACTIVE_LOW ? (irRaw == LOW) : (irRaw == HIGH);
  if (irFire) fireDetected = true;
}

// Gửi dữ liệu dạng JSON lên Firebase Realtime Database bằng phương thức PUT
int sendPayloadToFirebase(float aRound, int mqVal, bool isFire, bool isAlarm) {
  String payload = "{\"a_xy\":" + String(aRound, 3) +
                   ",\"mq2\":" + String(mqVal) +
                   ",\"fire\":" + String(isFire ? "true" : "false") +
                   ",\"alarm\":" + String(isAlarm ? "true" : "false") + "}";

  HTTPClient http;
  http.setTimeout(1500);
  http.begin(FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.PUT(payload);
  http.end();
  return httpCode;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[B1] Khoi dong ESP32...");

  pinMode(FlamePin, INPUT);
  analogReadResolution(12); // Độ phân giải ADC 12-bit (0..4095)

  Wire.begin(21, 22);
  Wire.setClock(400000);
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x6B); // Thanh ghi PWR_MGMT_1
  Wire.write(0x00); // Đánh thức MPU6050
  uint8_t mpuErr = Wire.endTransmission(true);
  Serial.print("[B2] MPU6050 status: ");
  Serial.println(mpuErr); // 0 là kết nối OK

  calibrateMpuXY(100);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false); // Giữ Wi-Fi phản hồi ổn định hơn
  Serial.print("[B3] Dang ket noi WiFi");
  uint32_t tStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - tStart > WIFI_TIMEOUT_MS) {
      Serial.println("\n[LOI] Qua 15s chua vao WiFi!");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n[B4] WiFi OK, IP: ");
    Serial.println(WiFi.localIP());
  }
}

void loop() {
  uint32_t now = millis();

  // Bước 1: Đọc cảm biến theo chu kỳ SAMPLE_MS
  if (now - lastSampleMs >= SAMPLE_MS) {
    lastSampleMs = now;
    sampleSensors();
  }

  // Bước 2: Đánh giá trạng thái cảnh báo tổng hợp
  bool quakeAlarm = (peakAccG >= ACC_THRESHOLD_G);
  bool gasAlarm = (peakMq2 >= MQ2_THRESHOLD);
  bool anyAlarm = quakeAlarm || gasAlarm || fireDetected;

  // Bước 3: Quyết định thời điểm gửi dữ liệu
  bool stateChanged = (anyAlarm != lastAlarmState);
  bool periodicSend = (now - lastSendMs >= SEND_MS) && (anyAlarm || peakAccG >= ACTIVE_SEND_G);

  if (WiFi.status() == WL_CONNECTED && (stateChanged || periodicSend)) {
    lastSendMs = now;
    float aRound = roundf(peakAccG * 1000.0f) / 1000.0f;
    int code = sendPayloadToFirebase(aRound, peakMq2, fireDetected, anyAlarm);

    Serial.print("HTTP code: ");
    Serial.print(code);
    Serial.print(" | a_xy: ");
    Serial.print(aRound, 3);
    Serial.print(" | mq2: ");
    Serial.print(peakMq2);
    Serial.print(" | fire: ");
    Serial.println(fireDetected);

    // Chỉ cập nhật trạng thái đã đồng bộ và reset đỉnh khi gửi thành công
    if (code > 0 && code < 400) {
      lastAlarmState = anyAlarm;
      peakAccG = 0.0f;
      peakMq2 = 0;
      fireDetected = false;
    }
  }
}
