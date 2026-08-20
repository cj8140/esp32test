#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>

//MPU6500,6050모듈 대부분은 AD0에 아무것도 연결하지 않았을때, 내부보드에서 풀다운저항을 사용 GND로 연결되어 기본주소 0x68
//내부모듈 불량, 접속불안정시 AD0이 플로팅상태. 노이즈에 따라 0x68, 0x69를 번갈아 연결
//전선으로 3.3V연결하면 0x69, GND에 연결하면 0x68로 안정화됨 

#define MPU6500_ADDR  0x68 // 8/19까지는 0x68로 작동했었음 20아침부터 작동안해 0x69로 바꿨는데도 계속 접속 못하는문제 발생
#define LED_PIN 3
#define BUZZER_PIN 5
#define SDA_PIN       8
#define SCL_PIN       9
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
} struct_message;
struct_message myData;
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
  Wire.setClock(400000);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
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
    startBuzzer(1);
  }

  if (countReady && filteredAngleY >= -20) { // 팔올림 1회 전송
    startBuzzer(2);

  // 데이터 전송
    myData.value = 1;
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
  updateBuzzer();
}


// 부저 멜로디 재생
void updateBuzzer() {
  if (!buzzerPlaying) {
    return;
  }

  int melodyDown[] = {523, 659};   // 도5, 미5
  int melodyUp[] = {784, 1047};    // 솔5, 도6
  int duration[] = {100, 130};

  int *melody;

  if (buzzerType == 1) {
    melody = melodyDown;
  }
  else {
    melody = melodyUp;
  }

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