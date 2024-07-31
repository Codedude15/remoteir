#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <IRsend.h>
#include <ir_Electra.h>

// Cấu hình kết nối WiFi
#define WIFI_SSID "IoT LAB"
#define WIFI_PASSWORD "kvt1ptit"

// Cấu hình Firebase
#define FIREBASE_HOST "fir-sp8266-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "NSRNwvkC1A4eW7ttBKtcjWTz0YCmlTs80Sv840e2"

// Chọn pin nhận tín hiệu IR
const uint16_t kRecvPin = D2; 

// Tốc độ baud của kết nối Serial
const uint32_t kBaudRate = 115200;

// Kích thước bộ đệm capture
const uint16_t kCaptureBufferSize = 1024;

// Thời gian timeout
const uint8_t kTimeout = 15;

// Tỉ lệ dung sai
const uint8_t kTolerancePercentage = kTolerance;

// Chân của IR LED phát
const int IR_LED_PIN = D7; // Chân D1 trên D1 Mini Pro

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
IRsend irsend(IR_LED_PIN);
decode_results results;

// Firebase objects
FirebaseData firebaseData;
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;

int irDataCount = 0; // Đếm số lượng tín hiệu IR đã lưu

// Cấu hình cho Electra AC
const uint16_t kIrLed = 13;  // ESP8266 GPIO pin to use.
IRElectraAc ac(kIrLed);
int currentTemp = 26;  // Nhiệt độ hiện tại, bắt đầu từ 26

void printState() {
  Serial.println("Electra A/C remote is in the following state:");
  Serial.printf("  %s\n", ac.toString().c_str());
  unsigned char* ir_code = ac.getRaw();
  Serial.print("IR Code: 0x");
  for (uint8_t i = 0; i < kElectraAcStateLength; i++)
    Serial.printf("%02X", ir_code[i]);
  Serial.println();
}

void setup() {
  // Thiết lập Serial
  Serial.begin(kBaudRate);
  while (!Serial) delay(50);
  assert(irutils::lowLevelSanityCheck() == 0);

  // Thiết lập IR receiver và IR sender
  irrecv.setTolerance(kTolerancePercentage);
  irrecv.enableIRIn();
  irsend.begin();

  // Kết nối WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());

  // Kết nối Firebase
  firebaseConfig.host = FIREBASE_HOST;
  firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true);

  // Cấu hình ban đầu cho điều hòa
  ac.begin();
  delay(100);
  ac.on();
  ac.setFan(kElectraAcFanAuto);
  ac.setMode(kElectraAcCool);
  ac.setTemp(currentTemp);
  printState();

  // Đọc và khôi phục dữ liệu IR từ Firebase
  Firebase.getInt(firebaseData, "/irDataCount");
  if (firebaseData.dataType() == "int") {
    irDataCount = firebaseData.intData();
    Serial.printf("Đã khôi phục %d tín hiệu IR từ Firebase.\n", irDataCount);
  }
}

void loop() {
  // Xử lý tín hiệu IR
  if (irrecv.decode(&results)) {
    uint32_t now = millis();
    Serial.printf("Thời gian: %06u.%03u\n", now / 1000, now % 1000);
    if (results.overflow)
      Serial.printf("Cảnh báo: Bộ đệm đầy (%d)\n", kCaptureBufferSize);
    Serial.println("Phiên bản thư viện: v" _IRREMOTEESP8266_VERSION_STR);
    if (kTolerancePercentage != kTolerance)
      Serial.printf("Dung sai: %d%%\n", kTolerancePercentage);

    // Hiển thị rawData
    String rawData = "{";
    for (uint16_t i = 1; i < results.rawlen; i++) {
      rawData += String(results.rawbuf[i] * kRawTick);
      if (i < results.rawlen - 1) rawData += ", ";
    }
    rawData += "}";
    Serial.println("rawData: " + rawData);
    Serial.println();

    // Đẩy dữ liệu lên Firebase
    String irDataPath = "/irData/irData" + String(irDataCount);
    String assignedKeyPath = "/assignedKeys/" + String(irDataCount + 1) + "/status";

    if (Firebase.setString(firebaseData, irDataPath, rawData)) {
      Serial.println("Dữ liệu IR được lưu lên Firebase thành công!");
      if (Firebase.setBool(firebaseData, assignedKeyPath, false)) {
        Serial.println("Địa chỉ giá trị đã được đặt thành false!");
      } else {
        Serial.println("Lỗi khi cập nhật giá trị boolean thành false: " + firebaseData.errorReason());
      }
      irDataCount++;
      // Cập nhật số lượng tín hiệu IR đã lưu
      if (Firebase.setInt(firebaseData, "/irDataCount", irDataCount)) {
        Serial.println("Số lượng tín hiệu IR đã lưu được cập nhật thành công!");
      } else {
        Serial.println("Lỗi khi cập nhật số lượng tín hiệu IR đã lưu: " + firebaseData.errorReason());
      }
    } else {
      Serial.println("Lỗi khi lưu dữ liệu IR lên Firebase: " + firebaseData.errorReason());
    }

    irrecv.resume();
  }

  // Kiểm tra trạng thái từ Firebase và thực hiện các lệnh tương ứng
  for (int i = 0; i < irDataCount; i++) {
    String assignedKeyPath = "/assignedKeys/" + String(i + 1) + "/status";
    if (Firebase.getBool(firebaseData, assignedKeyPath)) {
      bool status = firebaseData.boolData();
      if (status) {
        // Lấy tín hiệu IR từ Firebase
        String irDataPath = "/irData/irData" + String(i);
        if (Firebase.getString(firebaseData, irDataPath)) {
          String irSignal = firebaseData.stringData();
          Serial.println("Tín hiệu IR được lấy từ Firebase: " + irSignal);
          
          // Chuyển đổi chuỗi tín hiệu thô thành mảng uint16_t
          irSignal.replace("{", "");
          irSignal.replace("}", "");
          irSignal.replace(" ", "");
          irSignal.replace(",", " ");
          std::vector<uint16_t> rawDataVector;
          char* token = strtok(const_cast<char*>(irSignal.c_str()), " ");
          while (token != nullptr) {
            rawDataVector.push_back(static_cast<uint16_t>(atoi(token)));
            token = strtok(nullptr, " ");
          }
          uint16_t rawData[rawDataVector.size()];
          for (size_t j = 0; j < rawDataVector.size(); ++j) {
            rawData[j] = rawDataVector[j];
          }

          // Phát tín hiệu IR
          irsend.sendRaw(rawData, sizeof(rawData) / sizeof(rawData[0]), 38); 
          Serial.println("Tín hiệu IR đã được gửi.");

          // Đặt lại trạng thái thành false
          if (Firebase.setBool(firebaseData, assignedKeyPath, false)) {
            Serial.println("Trạng thái đã được đặt lại thành false!");
          } else {
            Serial.println("Lỗi khi đặt lại trạng thái: " + firebaseData.errorReason());
          }
        } else {
          Serial.println("Lỗi khi lấy dữ liệu IR từ Firebase: " + firebaseData.errorReason());
        }
      }
    } else {
      Serial.println("Lỗi khi kiểm tra trạng thái: " + firebaseData.errorReason());
    }
  }

  // Đọc trạng thái từ Firebase và thực hiện các lệnh tương ứng
  bool updateRequired = false;

  if (Firebase.getBool(firebaseData, "/button/on")) {
    if (firebaseData.boolData()) {
      ac.on();
      Serial.println("Turning on the A/C...");
      updateRequired = true;
      Firebase.setBool(firebaseData, "/button/on", false);
    }
  }

  if (Firebase.getBool(firebaseData, "/button/off")) {
    if (firebaseData.boolData()) {
      ac.off();
      Serial.println("Turning off the A/C...");
      updateRequired = true;
      Firebase.setBool(firebaseData, "/button/off", false);
    }
  }

  if (Firebase.getBool(firebaseData, "/button/+temp")) {
    if (firebaseData.boolData()) {
      if (currentTemp < 32) {
        currentTemp++;
        ac.setTemp(currentTemp);
        Serial.printf("Tăng nhiệt độ lên %d\n", currentTemp);
        updateRequired = true;
        Firebase.setBool(firebaseData, "/button/+temp", false);
      }
    }
  }

  if (Firebase.getBool(firebaseData, "/button/-temp")) {
    if (firebaseData.boolData()) {
      if (currentTemp > 16) {
        currentTemp--;
        ac.setTemp(currentTemp);
        Serial.printf("Giảm nhiệt độ xuống %d\n", currentTemp);
        updateRequired = true;
        Firebase.setBool(firebaseData, "/button/-temp", false);
      }
    }
  }
   
  if (Firebase.getBool(firebaseData, "/button/fan")) {
    if (firebaseData.boolData()) {
      // Chuyển đổi giữa các chế độ quạt
      if (ac.getFan() == kElectraAcFanAuto) {
        ac.setFan(kElectraAcFanHigh);
        Serial.println("Switching to High fan speed...");
      } else {
        ac.setFan(kElectraAcFanAuto);
        Serial.println("Switching to Auto fan speed...");
      } 
      updateRequired = true;
      Firebase.setBool(firebaseData, "/button/fan", false);
    }
  }

  // Điều khiển chế độ
  if (Firebase.getBool(firebaseData, "/button/mode")) {
    if (firebaseData.boolData()) {
      // Chuyển đổi giữa các chế độ
      switch (ac.getMode()) {
        case kElectraAcCool:
          ac.setMode(kElectraAcHeat);
          Serial.println("Đặt chế độ: Heat");
          break;
        case kElectraAcHeat:
          ac.setMode(kElectraAcDry);
          Serial.println("Đặt chế độ: Dry");
          break;
        case kElectraAcDry:
          ac.setMode(kElectraAcFan);
          Serial.println("Đặt chế độ: Fan");
          break;
        case kElectraAcFan:
          ac.setMode(kElectraAcCool);
          Serial.println("Đặt chế độ: Cool");
          break;
      }
      updateRequired = true;
      Firebase.setBool(firebaseData, "/button/mode", false);
    }
  }

  if (updateRequired) {
    ac.send();
    printState();
  }

  // Đợi một thời gian trước khi thực hiện vòng lặp tiếp theo
  delay(100);
}

