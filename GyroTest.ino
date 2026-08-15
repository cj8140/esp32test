//2026/08/15 
//Chat GPT  https://chatgpt.com/share/6a8036a9-60c0-83e8-988e-ab3a3b989ecb
//Preference Additional Board manger  https://espressif.github.io/arduino-esp32/package_esp32_index.json
//Board Manger : esp32
//Librar Manger : ESP32Servo
//보드선택은 tools - Boards - Esp32 - ESP32C3 Dev Module

#include <Wire.h>
#include <ESP32Servo.h>

#define MPU6500_ADDR 0x68
#define RESET_PIN 0
#define BUZZER_PIN 1
#define SERVO_PIN 3
#define LED_PIN 4
#define SDA_PIN 8 //white 1 wire
#define SCL_PIN 9 //white 0 wire

#define PWR_MGMT_1    0x6B// MPU6500 registers
#define ACCEL_CONFIG  0x1C
#define GYRO_CONFIG   0x1B
#define ACCEL_XOUT_H  0x3B

Servo servoMotor;

float ax, ay, az;// 센서값
float gx, gy, gz;
float angleY = 0;  // Y축 기준 자세각
float gyroYOffset = 0;  // 자이로 Y축 오프셋
unsigned long previousTime;  // 시간
int lastServoAngle = -1;  // 마지막으로 서보에 명령한 값
bool celebrationPlayed = false; // 부저음 한번재생용 

void writeRegister(byte reg, byte value) {// MPU6500 레지스터 쓰기
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

bool readMPU6500() {// MPU6500 전체 데이터 읽기 ACC X/Y/Z  온도  GYRO X/Y/Z  총 14바이트
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  Wire.requestFrom(MPU6500_ADDR, 14);
  if (Wire.available() < 14) {
    return false;
  }
  int16_t axRaw = (Wire.read() << 8) | Wire.read();
  int16_t ayRaw = (Wire.read() << 8) | Wire.read();
  int16_t azRaw = (Wire.read() << 8) | Wire.read();
  // 온도 데이터는 사용하지 않음
  Wire.read();
  Wire.read();
  int16_t gxRaw = (Wire.read() << 8) | Wire.read();
  int16_t gyRaw = (Wire.read() << 8) | Wire.read();
  int16_t gzRaw = (Wire.read() << 8) | Wire.read();
  // ±2g
  ax = axRaw / 16384.0;
  ay = ayRaw / 16384.0;
  az = azRaw / 16384.0;
  // ±250°/s
  gx = gxRaw / 131.0;
  gy = gyRaw / 131.0;
  gz = gzRaw / 131.0;
  return true;
}

void calibrateGyro() {// 자이로 오프셋 측정  전원을 켠 후 센서를 가만히 놓아야 함
  Serial.println();
  Serial.println("자이로 보정 시작");
  Serial.println("센서를 움직이지 마세요.");
  Serial.println("약 3초 동안 측정합니다.");
  Serial.println();
  delay(1000);
  const int samples = 1000;
  float sumY = 0;
  for (int i = 0; i < samples; i++) {
    readMPU6500();
    sumY += gy;
    delay(3);
  }
  gyroYOffset = sumY / samples;
  Serial.print("Gyro Y Offset = ");
  Serial.println(gyroYOffset, 3);
  Serial.println("보정 완료.");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);  // I2C 속도 400kHz
  delay(100);
  writeRegister(PWR_MGMT_1, 0x00);  // MPU6500 깨우기
  delay(100);
  writeRegister(ACCEL_CONFIG, 0x00);// 가속도 ±2g
  writeRegister(GYRO_CONFIG, 0x00);  // 자이로 ±250°/s
  delay(100);
  calibrateGyro();  // 자이로 보정
  servoMotor.attach(SERVO_PIN);  // 서보 연결
  servoMotor.write(142);  // 초기 위치
  lastServoAngle = 142;

  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  analogWrite(LED_PIN, 0);


  previousTime = micros();  // 시간 초기화
  Serial.println("================================");
  Serial.println("MPU6500 + SERVO START");
  Serial.println("Y AXIS CONTROL");
  Serial.println("================================");
  Serial.println();
}

void loop() {
  if (!readMPU6500()) {  // 센서 읽기
    Serial.println("MPU6500 I2C ERROR");
    delay(10);
    return;
  }

  unsigned long currentTime = micros();  // 시간 계산
  float dt = (currentTime - previousTime) / 1000000.0;
  previousTime = currentTime;

  if (dt <= 0 || dt > 0.1) dt = 0.01;  // 비정상적인 시간 간격 방지

  float accAngleY = atan2(ax, sqrt(ay * ay + az * az)) * 180.0 / PI;  // Y축 기울기 계산
  float gyroY = gy - gyroYOffset;  // 자이로 Y축 오프셋 제거
  angleY = 0.90 * (angleY + gyroY * dt) + 0.10 * accAngleY;  // Complementary Filter 매우안정 0.98:0.02 반응빠름 0.90:0.10
  angleY = constrain(angleY, -80, 0);  // 센서 각도 제한

// 증분 Incremental 제어코드============================================================================================
  static bool countReady = false;  // 증분 제어  Y축이 -65° 이하가 되면 카운트 준비 이후 -20° 이상이 되면 1회 카운트
  if (angleY <= -65) countReady = true;

  int moveAngle = map(analogRead(2), 0, 4095, 1, 8);
  if (countReady && angleY >= -20) { 
    lastServoAngle -= moveAngle;  // 서보 2° 감소, 감소가 윗방향. 64/1=64, /2=32, /3=21.3, /4=16, /5=12.8 /6=10.6, /7=9.14, /8=8
    lastServoAngle = constrain(lastServoAngle, 78, 142);
    servoMotor.write(lastServoAngle);
    countReady = false;
  }

  // 리셋 스위치
  if (digitalRead(RESET_PIN) == LOW) {
    for (int angle = lastServoAngle; angle <= 142; angle++) {
      servoMotor.write(angle);
      delay(40);
    }
    lastServoAngle = 142;
    celebrationPlayed = false;
    delay(300);
  }

  if (lastServoAngle <= 78) {
    analogWrite(LED_PIN, 128);  // 50%
    if (!celebrationPlayed) {
      playMarioStageClear();
      celebrationPlayed = true;
    }
  }
  else {
    analogWrite(LED_PIN, 0);
  }

  static unsigned long lastPrint = 0;  // 모니터 출력
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.print("Y Angle : ");
    Serial.print(angleY, 1);
    Serial.print(" deg   Servo : ");
    Serial.print(lastServoAngle);
    Serial.print(" deg");
    Serial.print(" Difficulty : ");
    Serial.println(moveAngle);
  }
// 증분 Incremental 제어코드============================================================================================


// 비례 Proportional 제어코드 ===========================================================================================
/*
  int servoAngle = map( (int)angleY, -80, 0, 142, 78);  // Y축 각도 → 서보 -90° → 142°  0° → 98° +90° → 54°
  servoAngle = constrain(servoAngle, 78, 142);

  if (servoAngle != lastServoAngle) {  // 서보값이 실제로 변할 때만 명령
    servoMotor.write(servoAngle);
    lastServoAngle = servoAngle;
  }

  static unsigned long lastPrint = 0;  // 모니터 출력
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.print("Y Angle : ");
    Serial.print(angleY, 1);
    Serial.print(" deg   Servo : ");
    Serial.print(servoAngle);
    Serial.print("   GyroY : ");
    Serial.print(gyroY, 2);
    Serial.println(" deg/s");
  }
*/
// 비례 Proportional 제어코드===========================================================================================
}

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
