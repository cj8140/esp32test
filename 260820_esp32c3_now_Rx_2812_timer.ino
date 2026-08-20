//ws2812 1st 8, 2nd 12, 3rd 16, 4th 24

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include <FastLED.h> // AUG 20
#include <LiquidCrystal_I2C.h>

#define RESET_PIN    0
#define BUZZER_PIN   1
#define SERVO_PIN    3
#define LED_PIN      4
#define BUTTON_PIN   5
#define NUM_LEDS     60
CRGB leds[NUM_LEDS];

// #define FND_CLK_PIN  5
// #define FND_DIO_PIN  6

//LCD1602
#define SDA_PIN      8
#define SCL_PIN      9

Servo servoMotor;

// TM1637Display display(FND_CLK_PIN, FND_DIO_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

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

// 게임 시간
bool timerRunning = false;
unsigned long timerStartTime = 0;
unsigned long timerStopTime = 0;

bool buttonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastButtonTime = 0;

// 폭죽 변수
bool fireworkRequest = false;
bool fireworkPlayed = false;

// 함수 선언
int getMaxCount(int level);
void updateDisplay(int difficulty, int remaining);
void playLevelUp();
void playTaDa();
void playMarioStageClear();
void playFirework();
void playFireworkWithMusic();
void setRingColor(int ring, uint8_t brightness);

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
  Serial.println("ESP-NOW RECEIVER + SERVO + LCD");
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
  /*
  display.setBrightness(7);
  display.clear();
  */

  // LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // WS2812
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(100);
  FastLED.clear();
  FastLED.show();

  // GPIO
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  // analogWrite(LED_PIN, 0);

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
  // 비상 버튼 입력
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentButtonState == LOW) {

    if (millis() - lastButtonTime >= 200) {
      receivedMove = true;
      lastButtonTime = millis();
      Serial.println("BUTTON : 1회 입력");
    }
  }

  lastButtonState = currentButtonState;


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

  //타이머시작
    if (!timerRunning && remainingCount > 0) {
      timerStartTime = millis();
      timerRunning = true;
      Serial.println("TIME START");
    }

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

    // LCD 갱신
    updateDisplay(difficulty, remainingCount);

    // 시리얼 출력
    Serial.print("1회 동작 처리");
    Serial.print("   Servo : ");
    Serial.print(lastServoAngle);
    Serial.print(" deg");
    Serial.print("   Remaining : ");
    Serial.println(remainingCount);

    if (remainingCount == 0 && !fireworkPlayed) {    // 게임 클리어 판정

      if (timerRunning) {
        timerStopTime = millis();
        timerRunning = false;
      }

      updateDisplay(difficulty, remainingCount);

      fireworkRequest = true;
    }
  }

  if (fireworkRequest && !fireworkPlayed) {  // 게임 클리어 연출
    fireworkRequest = false;
    fireworkPlayed = true;
    celebrationPlayed = true;

    Serial.println();
    Serial.println("================================");
    Serial.println("GAME CLEAR!");
    Serial.println("================================");

    playMarioStageClear();
    playFirework();
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
    fireworkPlayed = false;
    fireworkRequest = false;

    timerRunning = false;
    timerStartTime = 0;
    timerStopTime = 0;

    FastLED.clear();
    FastLED.show();

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

    playTaDa();
  }

  // 게임 시간 LCD 갱신
  static unsigned long lastTimeDisplay = 0;

  if (timerRunning && millis() - lastTimeDisplay >= 100) {
    lastTimeDisplay = millis();
    updateDisplay(difficulty, remainingCount);
  }

  // 게임 클리어
/*   if (lastServoAngle <= 78) {
    analogWrite(LED_PIN, 128);

    if (!celebrationPlayed) {
      playMarioStageClear();
      celebrationPlayed = true;
    }
  }

  else {
    analogWrite(LED_PIN, 0);
  } */

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

  // FND 표시
  /*
  uint8_t segments[4];

  segments[0] = display.encodeDigit(difficulty);
  segments[1] = 0x40;
  segments[2] = display.encodeDigit(remaining / 10);
  segments[3] = display.encodeDigit(remaining % 10);

  display.setSegments(segments);
  */



void updateDisplay(int difficulty, int remaining) {
  difficulty = constrain(difficulty, 1, 5);
  remaining = constrain(remaining, 0, 99);

  // 진행률 계산
  int maxCount = getMaxCount(difficulty);
  int completedCount = maxCount - remaining;
  int progressPercent = (completedCount * 100) / maxCount;

  // 10% 단위로 표시
  progressPercent = (progressPercent / 10) * 10;
  progressPercent = constrain(progressPercent, 0, 100);

  // LCD 첫째 줄
  lcd.setCursor(0, 0);
  lcd.print("LVL:");
  lcd.print(difficulty);
  lcd.print("  REM:");
  if (remaining < 10) lcd.print(" ");
  lcd.print(remaining);

  // LCD 둘째 줄
  unsigned long elapsedTime = 0;

  if (timerRunning) {
    elapsedTime = millis() - timerStartTime;
  }
  else if (timerStopTime > 0) {
    elapsedTime = timerStopTime - timerStartTime;
  }

  unsigned long totalCentiseconds = elapsedTime / 10;
  unsigned long minutes = totalCentiseconds / 6000;
  unsigned long seconds = (totalCentiseconds / 100) % 60;
  unsigned long centiseconds = totalCentiseconds % 100;

//T=0:00:00 P=100%

    lcd.setCursor(0, 1);
    lcd.print("T=");

    lcd.print(minutes);
    lcd.print(":");

    if (seconds < 10) lcd.print("0");
    lcd.print(seconds);
    lcd.print(":");

    if (centiseconds < 10) lcd.print("0");
    lcd.print(centiseconds);

    lcd.print(" P=");

    if (progressPercent < 100) lcd.print(" ");
    if (progressPercent < 10) lcd.print(" ");
    lcd.print(progressPercent);
    lcd.print("%");
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

void setRingColor(int ring, uint8_t brightness) {
  int startIndex = 0;
  int ledCount = 0;

  switch (ring) {
    case 1:
      startIndex = 0;
      ledCount = 8;
      break;
    case 2:
      startIndex = 8;
      ledCount = 12;
      break;
    case 3:
      startIndex = 20;
      ledCount = 16;
      break;
    case 4:
      startIndex = 36;
      ledCount = 24;
      break;
  }

  for (int i = 0; i < ledCount; i++) {
    leds[startIndex + i] = CRGB(brightness, 0, 0);
  }

  FastLED.show();
}

// 폭죽 효과
void playFirework() {

  // 폭죽 색상
  CRGB colors[] = {
    CRGB::Red,
    CRGB::Orange,
    CRGB::Yellow,
    CRGB::Green,
    CRGB::Blue,
    CRGB::Purple,
    CRGB::Cyan,
    CRGB::Magenta
  };

  int colorCount = sizeof(colors) / sizeof(colors[0]);

  FastLED.clear();
  FastLED.show();

  // 1 → 2 → 3 → 4번 링 순서로 확산
  for (int ring = 1; ring <= 4; ring++) {

    int startIndex = 0;
    int ledCount = 0;

    switch (ring) {
      case 1:
        startIndex = 0;
        ledCount = 8;
        break;

      case 2:
        startIndex = 8;
        ledCount = 12;
        break;

      case 3:
        startIndex = 20;
        ledCount = 16;
        break;

      case 4:
        startIndex = 36;
        ledCount = 24;
        break;
    }

    // 링마다 랜덤 색상 선택
    CRGB baseColor = colors[random(0, colorCount)];

    // 링 내부에 여러 색상 배치
    for (int i = 0; i < ledCount; i++) {

      int colorIndex = random(0, colorCount);

      // 기본 색상을 선택하거나 다른 랜덤 색상 사용
      if (random(0, 3) == 0) {
        leds[startIndex + i] = colors[colorIndex];
      }
      else {
        leds[startIndex + i] = baseColor;
      }
    }

    // 링 순간 점등
    FastLED.show();
    delay(120);

    // 천천히 Fade Out
    for (int brightness = 255; brightness >= 0; brightness -= 10) {

      for (int i = 0; i < ledCount; i++) {
        leds[startIndex + i].nscale8(brightness);
      }

      FastLED.show();
      delay(25);
    }
  }

  // 마지막 전체 랜덤 반짝임
  unsigned long sparkleStart = millis();

  while (millis() - sparkleStart < 4000) {

    for (int i = 0; i < NUM_LEDS; i++) {

      if (random(0, 3) == 0) {
        leds[i] = colors[random(0, colorCount)];
        leds[i].nscale8(random(80, 256));
      }
      else {
        leds[i].nscale8(120);
      }
    }

    FastLED.show();
    delay(50);
  }

  FastLED.clear();
  FastLED.show();
}