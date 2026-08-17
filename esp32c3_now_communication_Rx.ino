#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include <TM1637Display.h>

#define RESET_PIN    0
#define BUZZER_PIN   1
#define SERVO_PIN    3
#define LED_PIN      4

#define FND_CLK_PIN  5
#define FND_DIO_PIN  6

Servo servoMotor;
TM1637Display display(FND_CLK_PIN, FND_DIO_PIN);

// ESP-NOW 데이터
typedef struct {
  int value;
} DataPacket;
DataPacket incomingData;
volatile bool receivedMove = false;

// 게임 변수
int difficulty = 1;
int remainingCount = 64;
float moveAngle = 1.0;
float servoPosition = 142.0;
int lastServoAngle = 142;
bool celebrationPlayed = false;

// 함수 선언
int getMaxCount(int level);
void updateDisplay(int difficulty, int remaining);
void playLevelUp();
void playTaDa();
void playMarioStageClear();

// ESP-NOW 수신
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingDataPtr, int len) {
  if (len != sizeof(incomingData)) {
    return;
  }
  memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));
  if (incomingData.value == 1) {
    receivedMove = true;
    Serial.println("ESP-NOW : 1회 수신");
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000)) {
    delay(10);
  }
  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);  // Wi-Fi 절전 모드 OFF
  // 프로토콜 및 채널 고정
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // 근거리 통신용 출력

  // ESP-NOW
  Serial.println();
  Serial.println("================================");
  Serial.println("ESP-NOW RECEIVER + SERVO + FND");
  Serial.println("================================");

  Serial.print("My MAC : ");
  Serial.println(WiFi.macAddress());

  Serial.print("WiFi Channel : ");
  Serial.println(WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW 초기화 실패");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  // ESP-NOW 속도 1Mbps
  esp_wifi_config_espnow_rate(
    WIFI_IF_STA,
    WIFI_PHY_RATE_1M_L
  );

  Serial.println("ESP-NOW 수신 준비 완료");

  // 서보
  servoMotor.attach(SERVO_PIN);
  servoMotor.write(142);
  lastServoAngle = 142;
  servoPosition = 142.0;
  // FND
  display.setBrightness(7);
  display.clear();

  // GPIO
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  analogWrite(LED_PIN, 0);

  // 최초 난이도 설정
  difficulty = map(analogRead(2), 0, 4095, 1, 5);
  remainingCount = getMaxCount(difficulty);
  moveAngle = 64.0 / remainingCount;
  updateDisplay(difficulty, remainingCount);

  Serial.println();
  Serial.print("Difficulty : ");
  Serial.println(difficulty);
  Serial.print("Remaining : ");
  Serial.println(remainingCount);
  Serial.print("MoveAngle : ");
  Serial.println(moveAngle, 2);
  Serial.println();
}

void loop() {
  // 초기 위치에서 가변저항으로 난이도 변경
  if (lastServoAngle == 142) {
    int newDifficulty = map(analogRead(2), 0, 4095, 1, 5);

    if (newDifficulty != difficulty) {
      difficulty = newDifficulty;
      remainingCount = getMaxCount(difficulty);
      moveAngle = 64.0 / remainingCount;
      updateDisplay(difficulty, remainingCount);

      Serial.print("Difficulty : ");
      Serial.println(difficulty);
      Serial.print("Remaining : ");
      Serial.println(remainingCount);
      Serial.print("MoveAngle : ");
      Serial.println(moveAngle, 2);
    }
  }

  if (receivedMove) {  // ESP-NOW 1회 수신
    receivedMove = false;

    servoPosition -= moveAngle;    // 서보 위치 감소
    if (servoPosition < 78.0) {
      servoPosition = 78.0;
    }
    lastServoAngle = round(servoPosition);
    servoMotor.write(lastServoAngle);
    // 남은 횟수 감소
    remainingCount--;
    if (remainingCount < 0) {
      remainingCount = 0;
    }

    // FND 갱신
    updateDisplay(difficulty, remainingCount);
    // 시리얼 출력
    Serial.print("1회 동작 처리");
    Serial.print("   Servo : ");
    Serial.print(lastServoAngle);
    Serial.print(" deg");
    Serial.print("   Remaining : ");
    Serial.println(remainingCount);
  }
  // 리셋 스위치
  if (digitalRead(RESET_PIN) == LOW) {
    for (int angle = lastServoAngle; angle <= 142; angle++) {    // 서보를 142°까지 복귀
      servoMotor.write(angle);
      delay(40);
    }
    lastServoAngle = 142;    // 게임 상태 초기화
    servoPosition = 142.0;
    celebrationPlayed = false;
    receivedMove = false;

    difficulty = map(analogRead(2), 0, 4095, 1, 5);    // 리셋 후 현재 가변저항값으로 난이도 설정
    remainingCount = getMaxCount(difficulty);
    moveAngle = 64.0 / remainingCount;
    updateDisplay(difficulty, remainingCount);

    Serial.println();
    Serial.println("================================");
    Serial.println("RESET");
    Serial.print("Difficulty : ");
    Serial.println(difficulty);
    Serial.print("Remaining : ");
    Serial.println(remainingCount);
    Serial.print("MoveAngle : ");
    Serial.println(moveAngle, 2);
    Serial.println("================================");
    Serial.println();
    delay(300);
  }

  // 게임 클리어
  if (lastServoAngle <= 78) {
    analogWrite(LED_PIN, 128);
    if (!celebrationPlayed) {
      playMarioStageClear();
      celebrationPlayed = true;
    }
  }

  else {
    analogWrite(LED_PIN, 0);
  }
  // 시리얼 모니터
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.print("Servo : ");
    Serial.print(lastServoAngle);
    Serial.print(" deg   Difficulty : ");
    Serial.print(difficulty);
    Serial.print("   Remaining : ");
    Serial.print(remainingCount);
    Serial.print("   MoveAngle : ");
    Serial.println(moveAngle, 2);
  }

// 비례 Proportional 제어코드
/*
  // ESP-NOW 방식에서는 사용하지 않음
  // 기존 원보드에서 사용했던 비례제어 코드 보존
  int servoAngle =
    map((int)filteredAngleY, -80, 0, 142, 78);

  servoAngle = constrain(servoAngle, 78, 142);

  if (servoAngle != lastServoAngle) {
    servoMotor.write(servoAngle);
    lastServoAngle = servoAngle;
  }

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.print("Y Angle : ");
    Serial.print(filteredAngleY, 1);
    Serial.print(" deg   Servo : ");
    Serial.print(servoAngle);
    Serial.println(" deg");
  }
*/
// 비례 Proportional 제어코드 끝
}

// 난이도 → 목표 횟수
int getMaxCount(int level) {
  switch (level) {
    case 1: return 64;
    case 2: return 40;
    case 3: return 28;
    case 4: return 18;
    case 5: return 12;
  }
  return 64;
}
// FND 표시
// 1-64
// 2-40
// 3-28
// 4-18
// 5-12

void updateDisplay(int difficulty, int remaining) {
  difficulty = constrain(difficulty, 1, 5);
  remaining = constrain(remaining, 0, 99);
  uint8_t segments[4];
  segments[0] = display.encodeDigit(difficulty);
  segments[1] = 0x40;
  segments[2] = display.encodeDigit(remaining / 10);
  segments[3] = display.encodeDigit(remaining % 10);

  display.setSegments(segments);
}

// 레벨업 음악
void playLevelUp() {
  int melody[] = {784, 1047, 1319, 1568};
  int duration[] = {100, 100, 100, 300};

  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(duration[i] * 1.2);
  }
  noTone(BUZZER_PIN);
}

// Ta-Da 음악
void playTaDa() {
  tone(BUZZER_PIN, 523, 100);
  delay(120);
  tone(BUZZER_PIN, 523, 100);
  delay(120);
  tone(BUZZER_PIN, 698, 200);
  delay(220);
  tone(BUZZER_PIN, 880, 200);
  delay(220);
  tone(BUZZER_PIN, 1047, 500);
  delay(550);
  noTone(BUZZER_PIN);
}

// Mario Stage Clear
void playMarioStageClear() {
  // G5, C6, E6, G6, C7, E7, G7, E7
  // G#5, C6, D#6, G#6
  // A#5, D6, F6, A#6, C7
  int melody[] = {
    784, 1047, 1319,
    1568, 2093, 2637,
    3136, 2637,
    831, 1047, 1245, 1661,
    932, 1175, 1397, 1865, 2093
  };

  int duration[] = {
    140, 140, 140,
    140, 140, 140,
    280, 280,
    140, 140, 140, 280,
    140, 140, 140, 140, 600
  };
  int totalNotes = sizeof(melody) / sizeof(melody[0]);
  for (int i = 0; i < totalNotes; i++) {
    tone(
      BUZZER_PIN,
      melody[i],
      duration[i]
    );
    delay(duration[i] * 1.25);
  }
  noTone(BUZZER_PIN);
}