// Rockannon 발신부  변경 이력
// ================================================================
// V1.0  2026-08-12 : -ESP32-C3 발신부 기본 구성, MPU6500 가속도 센서를 이용한 팔 동작 감지, 팔을 내렸다가 올리면 ESP-NOW로 1회 동작 전송
//                   -수신부 MAC 주소 등록 및 ESP-NOW 통신 구성, 동작 성공 시 LED 점등
// V1.1  2026-08-13 : -MPU6500 Y축 절대각도 계산 방식 적용, 팔 내림 
//       올림 각도를 이용한 동작 판정, 팔 내림 : -50° 이하, 팔 올림 : -20° 이상, 동작 감지 후 1회만 전송되도록 countReady 적용
// V1.2  2026-08-14 : -MPU6500 EMA 필터 적용, alpha 값을 이용한 각도 변화 안정화, 필터링된 각도를 -80° ~ 0° 범위로 제한
// V1.3  2026-08-19 : -팔을 내렸을 때와 올렸을 때의 부저음 추가, 동작 단계에 따라 서로 다른 음을 출력, ESP-NOW 전송 성공 시 LED 점등
// V1.4  2026-08-20 : -팔을 올렸을 때만 2음절 부저음 재생, 팔을 내렸을 때 부저음을 제거하여 동작 완료 시점을 명확하게 표시
//         -2음절 "띠링" 효과를 빠르게 재생하도록 음 길이 조정
//
// V1.5  2026-08-21 : -발신부 리셋 버튼 추가, RESET_PIN을 이용하여 본체 리셋 명령을 ESP-NOW로 전송
//     -ESP-NOW 데이터에 value / reset 두 가지 명령 추가, 리셋 버튼 Edge Detection 적용 → 버튼을 누르는 순간 1회만 전송
// V1.6  2026-08-21 : -ESP-NOW 동작 데이터를 value = 1, reset = 0으로 명확하게 설정, 리셋 데이터를 value = 0, reset = 1로 설정
//  -팔을 올렸을 때 정상적인 1회 동작 명령 전송 확인, 발신부 리셋 버튼으로 수신부 본체 리셋 및 리셋 음원 재생
// ================================================================
// 현재 버전 : V1.6
// ================================================================

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>

#define LED_PIN 3
#define BUZZER_PIN 5
#define SDA_PIN       6
#define SCL_PIN       7
#define RESET_PIN    10
#define MPU6500_ADDR  0x68

#define PWR_MGMT_1    0x6B
#define WHO_AM_I      0x75
#define ACCEL_XOUT_H  0x3B

// 수신부 ESP32-C3 MAC 주소
uint8_t receiverMac[] = {
  0x44, 0xB1, 0x76, 0x19, 0x8D, 0x4C
};

// ESP-NOW 데이터
typedef struct struct_message {
  int value;
  int reset;
} struct_message;
struct_message myData = {};

esp_now_peer_info_t peerInfo = {};

// MPU6500 변수
float ax, ay, az;
float filteredAngleY = 0;
const float alpha = 0.30; // Ori = 0.15

// 동작 카운트
int count = 0;
bool countReady = false;

void startBuzzer(int type);
void updateBuzzer();
void playReady();
void playMPUError();

// LED 표식
bool ledOn = false;
unsigned long ledStartTime = 0;

// 부저 멜로디
bool buzzerPlaying = false;
int buzzerStep = 0;
int buzzerType = 0;
unsigned long buzzerStartTime = 0;

// MPU6500 레지스터 쓰기
bool writeMPU6500(byte reg, byte value) {
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// MPU6500 WHO_AM_I 확인
bool checkMPU6500() {
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(WHO_AM_I);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  
  Wire.requestFrom(MPU6500_ADDR, 1);
  if (Wire.available() < 1) {
    return false;
  }
  byte whoAmI = Wire.read();
  Serial.print("WHO_AM_I : 0x");
  Serial.println(whoAmI, HEX);
  return whoAmI == 0x70;
}

// MPU6500 초기화
bool initMPU6500() {
  if (!checkMPU6500()) {
    Serial.println("MPU6500 연결 실패");
    return false;
  }
  if (!writeMPU6500(PWR_MGMT_1, 0x00)) {
    Serial.println("MPU6500 초기화 실패");
    return false;
  }
  delay(100);
  Serial.println("MPU6500 초기화 성공");
  return true;
}

// MPU6500 가속도 읽기
bool readMPU6500Accel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  Wire.requestFrom(MPU6500_ADDR, 6);
  if (Wire.available() < 6) {
    return false;
  }
  int16_t axRaw = (Wire.read() << 8) | Wire.read();
  int16_t ayRaw = (Wire.read() << 8) | Wire.read();
  int16_t azRaw = (Wire.read() << 8) | Wire.read();
  // MPU6500 ±2g
  ax = axRaw / 16384.0;
  ay = ayRaw / 16384.0;
  az = azRaw / 16384.0;
  return true;
}

// ESP-NOW 전송 완료 콜백
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("전송 상태: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "성공" : "실패");

  if (status == ESP_NOW_SEND_SUCCESS) {
    analogWrite(LED_PIN, 120);
    ledOn = true;
    ledStartTime = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);
//  analogWrite(LED_PIN, 125);

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // MPU6500 초기화
  if (!initMPU6500()) {
    Serial.println("MPU6500을 확인하세요.");

    while (1) {
      playMPUError();
      delay(1000);
    }
  }

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Wi-Fi 절전 모드 OFF
  esp_wifi_set_ps(WIFI_PS_NONE);
  // Wi-Fi 프로토콜 및 채널 고정

  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  // 송신 파워
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // ESP-NOW 시작
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW 초기화 실패");
    while (1) {
      delay(1000);
    }
  }
  esp_now_register_send_cb(OnDataSent);

  // 수신부 등록
  memcpy(peerInfo.peer_addr, receiverMac, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  esp_now_del_peer(receiverMac);
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("수신부 등록 실패");
    while (1) {
      delay(1000);
    }
  }

  // ESP-NOW 전송 속도 1Mbps
  esp_wifi_config_espnow_rate( WIFI_IF_STA, WIFI_PHY_RATE_1M_L);
  // 시작 메시지
  Serial.println();
  Serial.println("================================");
  Serial.println("ESP-NOW TRANSMITTER");
  Serial.println("================================");
  Serial.print("My MAC : ");
  Serial.println(WiFi.macAddress());
  Serial.println("WiFi Channel : 1");
  Serial.println("ESP-NOW 준비 완료");
  Serial.println();
  Serial.println("팔을 -65° 이하로 내렸다가");
  Serial.println("-20° 이상으로 올리면 1회 전송");
  Serial.println();

  playReady();
}

void loop() {

  // 본체 리셋 버튼
  static bool lastResetState = HIGH;
  bool currentResetState = digitalRead(RESET_PIN);

  if (lastResetState == HIGH && currentResetState == LOW) {
    myData.value = 0;
    myData.reset = 1;

    esp_err_t result = esp_now_send(
      receiverMac,
      (uint8_t *)&myData,
      sizeof(myData)
    );

    if (result == ESP_OK) {
      Serial.println("본체 리셋 명령 전송");
    }
    else {
      Serial.println("본체 리셋 명령 오류");
    }

    countReady = false;
    filteredAngleY = 0;
  }

  lastResetState = currentResetState;

  // MPU6500 읽기
  if (!readMPU6500Accel(ax, ay, az)) {
    Serial.println("MPU6500 I2C ERROR");
    delay(10);
    return;
  }

  // Y축 절대각도 계산
  float rawAccAngleY =
    atan2(ax, sqrt(ay * ay + az * az)) * 180.0 / PI;

  // EMA 필터
  filteredAngleY = (alpha * rawAccAngleY) + ((1.0 - alpha) * filteredAngleY);
  filteredAngleY = constrain(filteredAngleY, -80, 0);

  // 동작 판정
  if (filteredAngleY <= -50 && !countReady) { // 팔내림
    countReady = true;
  }

  if (countReady && filteredAngleY >= -20) { // 팔올림 1회 전송
    startBuzzer(2);

    // 데이터 전송
    myData.value = 1;
    myData.reset = 0;

    esp_err_t result = esp_now_send(
      receiverMac,
      (uint8_t *)&myData,
      sizeof(myData)
    );

    if (result == ESP_OK) {
      Serial.println("동작 1회 전송");
    }
    else {
      Serial.println("전송 명령 오류");
    }
    countReady = false;
  }

  // 시리얼 모니터
  static unsigned long lastPrint = 0;

  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.print("Angle : ");
    Serial.print(filteredAngleY, 1);
    Serial.print("   Count : ");
    Serial.println(count);
  }

  if (ledOn && millis() - ledStartTime >= 200) {
    analogWrite(LED_PIN, 0);
    ledOn = false;
  }
  updateBuzzer();
  delay(20);
}

// 부저 멜로디 시작
void startBuzzer(int type) {
  buzzerType = type;
  buzzerStep = 0;
  buzzerStartTime = millis();
  buzzerPlaying = true;

  if (buzzerType == 2) {
    tone(BUZZER_PIN, 784, 90);   // 띠
  }
}

// 부저 멜로디 재생
void updateBuzzer() {
  if (!buzzerPlaying) {
    return;
  }

  int melody[] = {784, 1047};    // 솔5, 도6
  int duration[] = {100, 150};

  if (millis() - buzzerStartTime >= duration[buzzerStep]) {
    buzzerStep++;

    if (buzzerStep >= 2) {
      noTone(BUZZER_PIN);
      buzzerPlaying = false;
      return;
    }

    buzzerStartTime = millis();
    tone(BUZZER_PIN, melody[buzzerStep], duration[buzzerStep]);
  }
}

// MPU6500 오류음
void playMPUError() {
  tone(BUZZER_PIN, 1000, 150);
  delay(220);

  tone(BUZZER_PIN, 1000, 150);
  delay(220);

  tone(BUZZER_PIN, 1000, 150);
  delay(220);

  noTone(BUZZER_PIN);
}

// 준비 완료음
void playReady() {
  tone(BUZZER_PIN, 523, 80);
  delay(200);

  tone(BUZZER_PIN, 659, 80);
  delay(200);

  tone(BUZZER_PIN, 784, 80);
  delay(200);

  tone(BUZZER_PIN, 1047, 80);
  delay(200);

  tone(BUZZER_PIN, 1319, 160);
  delay(280);

  tone(BUZZER_PIN, 1568, 250);
  delay(1080);

  noTone(BUZZER_PIN);
}
