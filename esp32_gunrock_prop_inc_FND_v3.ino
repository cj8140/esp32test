//2026/08/15 
//2026/08/16 FND v2 _5 level 1.6도 작동
//2026/08/16 FND v3 _5 아들가속도코드 + level getMaxCount 값조절 + 가속도센서오류 자동리셋
//Chat GPT  https://chatgpt.com/share/6a81f904-7430-83ee-aecc-ef202ea23b8f
//Preference Additional Board manger  https://espressif.github.io/arduino-esp32/package_esp32_index.json
//Board Manger : esp32
//Librar Manger : ESP32Servo
//보드선택은 tools - Boards - Esp32 - ESP32C3 Dev Module

#include <Wire.h>
#include <ESP32Servo.h>
#include <TM1637Display.h>

#define MPU6500_ADDR 0x68

#define RESET_PIN    0
#define BUZZER_PIN   1
#define SERVO_PIN    3
#define LED_PIN      4
#define FND_CLK_PIN  5
#define FND_DIO_PIN  6
#define SDA_PIN      8
#define SCL_PIN      9
#define PWR_MGMT_1   0x6B
#define ACCEL_XOUT_H 0x3B

Servo servoMotor;
TM1637Display display(FND_CLK_PIN, FND_DIO_PIN);

bool readMPU6500Accel(float &ax, float &ay, float &az);
void initMPU6500();
int getMaxCount(int level);
void updateDisplay(int difficulty, int remaining);
void playLevelUp();
void playTaDa();
void playMarioStageClear();

float ax, ay, az;
float filteredAngleY = 0; // EMA 필터를 적용한 Y축 각도
const float alpha = 0.15; // EMA 필터 계수. 작을수록 부드럽고 느림. 클수록 빠르게 반응
int i2cErrorCount = 0;// I2C 연속 오류 횟수
int difficulty = 1; // 게임 변수
int remainingCount = 64;
float moveAngle = 1.0; // 1회 동작마다 이동할 서보 각도
float servoPosition = 142.0; // 실제 서보 위치
int lastServoAngle = -1; // 마지막으로 서보에 명령한 각도
bool celebrationPlayed = false; // 클리어 음악 재생 여부
bool readMPU6500Accel(float &ax, float &ay, float &az) { // MPU6500 가속도 데이터 읽기
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(MPU6500_ADDR, 6);
  if (Wire.available() < 6) return false;

  int16_t axRaw = (Wire.read() << 8) | Wire.read();
  int16_t ayRaw = (Wire.read() << 8) | Wire.read();
  int16_t azRaw = (Wire.read() << 8) | Wire.read();

  ax = axRaw / 16384.0;  // MPU6500 ±2g
  ay = ayRaw / 16384.0;
  az = azRaw / 16384.0;
  return true;
}

void initMPU6500() { // MPU6500 초기화
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(PWR_MGMT_1);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(100);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(SDA_PIN, SCL_PIN);  // I2C
  Wire.setClock(400000);
  delay(100);
  initMPU6500();  // MPU6500 초기화
  servoMotor.attach(SERVO_PIN);
  servoMotor.write(142);
  lastServoAngle = 142;
  servoPosition = 142.0;
  display.setBrightness(7);  // FND
  display.clear();

  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  analogWrite(LED_PIN, 0);

  Serial.println("================================");  // 시작 메시지
  Serial.println("MPU6500 ACCEL + SERVO START");
  Serial.println("Y AXIS CONTROL");
  Serial.println("EMA FILTER");
  Serial.println("I2C AUTO RECOVERY");
  Serial.println("================================");
  Serial.println();
}

void loop() {
  if (!readMPU6500Accel(ax, ay, az)) {  // MPU6500 가속도 읽기
    Serial.println("MPU6500 I2C ERROR");
    i2cErrorCount++;
    if (i2cErrorCount >= 10) {    // 연속 10회 오류 발생 시 MPU6500 재초기화
      Serial.println("MPU6500 재초기화");
      initMPU6500();
      i2cErrorCount = 0;
    }
    delay(10);
    return;
  }
  i2cErrorCount = 0;  // 정상 통신이면 오류 횟수 초기화
  float rawAccAngleY = atan2(ax, sqrt(ay * ay + az * az))* 180.0 / PI; // Y축 절대각도 계산
  filteredAngleY = (alpha * rawAccAngleY) + ((1.0 - alpha) * filteredAngleY);  // EMA 필터
  filteredAngleY = constrain(filteredAngleY, -80, 0);  // V2에서 사용했던 각도 범위 제한
  static bool countReady = false;  // 증분 Incremental 제어

  if (lastServoAngle == 142) {  // 서보가 초기 위치일 때 난이도 설정
    difficulty = map(analogRead(2), 0, 4095, 1, 5);
    remainingCount = getMaxCount(difficulty);

    moveAngle = 64.0 / remainingCount;
    updateDisplay( difficulty, remainingCount);
  }
  if (filteredAngleY <= -65) countReady = true;  // 팔을 아래쪽으로 내림
  if (countReady && filteredAngleY >= -20) {  // 팔을 다시 올림 → 1회 인정
    servoPosition -= moveAngle;    // 서보 위치 감소
    if (servoPosition < 78.0) servoPosition = 78.0;  // 최소 위치 제한
    lastServoAngle = round(servoPosition);    // 실제 서보 명령값
    servoMotor.write(lastServoAngle);
    remainingCount--;    // 남은 횟수 감소
    if (remainingCount < 0) remainingCount = 0;
    updateDisplay(difficulty, remainingCount);    // FND 갱신
    countReady = false;    // 다음 동작 대기
  }

  if (digitalRead(RESET_PIN) == LOW) {  // 리셋 스위치
    for (int angle = lastServoAngle; angle <= 142; angle++) {    // 서보를 142°까지 천천히 복귀
      servoMotor.write(angle);
      delay(40);
    }

    lastServoAngle = 142;    // 게임 상태 초기화
    servoPosition = 142.0;
    celebrationPlayed = false;
    countReady = false;
    difficulty = map(analogRead(2), 0, 4095, 1, 5);    // 리셋 후 가변저항으로 난이도 설정
    remainingCount = getMaxCount(difficulty);    // 난이도에 따른 목표 횟수
    moveAngle = 64.0 / remainingCount;    // 64°를 목표 횟수로 나눔
    updateDisplay(difficulty, remainingCount);    // FND 갱신
    delay(300);
  }

  if (lastServoAngle <= 78) {  // 게임 클리어
    analogWrite(LED_PIN, 128);    // LED 50%
    if (!celebrationPlayed) {    // 클리어 음악은 한 번만
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
    Serial.print("Raw Angle : ");
    Serial.print(rawAccAngleY, 1);
    Serial.print("   Filtered Angle : ");
    Serial.print(filteredAngleY, 1);
    Serial.print("   Servo : ");
    Serial.print(lastServoAngle);
    Serial.print(" deg");
    Serial.print("   Difficulty : ");
    Serial.print(difficulty);
    Serial.print("   Remaining : ");
    Serial.print(remainingCount);
    Serial.print("   MoveAngle : ");
    Serial.println(moveAngle, 2);
  }
// 비례 Proportional 제어코드 ============================================================
/*
  int servoAngle = map((int)filteredAngleY, -80, 0, 142, 78);
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
// 비례 Proportional 제어코드 끝 ============================================================
}

int getMaxCount(int level) {// 난이도 → 목표 횟수
  switch (level) {
    case 1:return 64;
    case 2:return 54;
    case 3:return 44;
    case 4:return 34;
    case 5:return 24;
  }
  return 64;
}

// FND 표시 1-64 / 2-40 / 3-28/ 4-18/ 5-12
void updateDisplay(int difficulty,int remaining) {
  difficulty = constrain(difficulty, 1, 5);
  remaining = constrain(remaining, 0, 99);
  uint8_t segments[4];
  segments[0] = display.encodeDigit(difficulty); //첫 번째 자리
  segments[1] = 0x40;  // 두 번째 자리 "-"
  segments[2] = display.encodeDigit(remaining / 10);// 세 번째 자리
  segments[3] = display.encodeDigit(remaining % 10);  // 네 번째 자리
  display.setSegments(segments);
}

// 레벨업 음악
void playLevelUp() {
  int melody[] = {784, 1047, 1319, 1568}; // 솔(G5), 도(C6), 미(E6), 솔(G6)
  int duration[] = {100, 100, 100, 300};

  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(duration[i] * 1.2);
  }
  noTone(BUZZER_PIN);
}

void playTaDa() {
  tone(BUZZER_PIN, 523, 100); delay(120); // 도(C5)
  tone(BUZZER_PIN, 523, 100); delay(120); // 도(C5)
  tone(BUZZER_PIN, 698, 200); delay(220); // 파(F5)
  tone(BUZZER_PIN, 880, 200); delay(220); // 라(A5)
  tone(BUZZER_PIN, 1047, 500); delay(550); // 높은 도(C6)
  noTone(BUZZER_PIN);
}

void playMarioStageClear() {
  // 음 주파수(Hz): G5, C6, E6, G6, C7, E7, G7, E7, G#5, C6, D#6, G#6, A#5, D6, F6, A#6
  int melody[] = {
    784, 1047, 1319,               // 솔, 도, 미
    1568, 2093, 2637,              // 높은 솔, 도, 미
    3136, 2637,                    // 최고음 솔, 미
    831, 1047, 1245, 1661,         // 솔#, 도, 레#, 솔#
    932, 1175, 1397, 1865, 2093    // 라#, 레, 파, 라#, 높은 도
  };

  // 각 음의 재생 시간(ms) - 박자를 늘려 유연하게 연주
  int duration[] = {
    140, 140, 140, 
    140, 140, 140, 
    280, 280, 
    140, 140, 140, 280, 
    140, 140, 140, 140, 600
  };
  int totalNotes = sizeof(melody) / sizeof(melody[0]);

  for (int i = 0; i < totalNotes; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    // 음과 음 사이 간격을 살짝 주어 또렷하게 들리도록 설정
    delay(duration[i] * 1.25); 
  }
  noTone(BUZZER_PIN);
}
