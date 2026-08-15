#include <Wire.h>

#define MPU6500_ADDR 0x68
#define SDA_PIN 8 
#define SCL_PIN 9 

#define PWR_MGMT_1    0x6B
#define ACCEL_XOUT_H  0x3B

float filteredAngleY = 0;
const float alpha = 0.15; // EMA 필터 계수 (값이 작을수록 부드럽고, 클수록 반응이 빨라짐)

bool readMPU6500Accel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  
  Wire.requestFrom(MPU6500_ADDR, 6); // 가속도 X, Y, Z 3축 데이터만 읽기 (총 6바이트)
  if (Wire.available() < 6) {
    return false;
  }
  
  int16_t axRaw = (Wire.read() << 8) | Wire.read();
  int16_t ayRaw = (Wire.read() << 8) | Wire.read();
  int16_t azRaw = (Wire.read() << 8) | Wire.read();
  
  // ±2g 설정 기준 값 변환
  ax = axRaw / 16384.0;
  ay = ayRaw / 16384.0;
  az = azRaw / 16384.0;
  
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);  // I2C 400kHz
  delay(100);

  // MPU6500 슬립 해제 (깨우기)
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(PWR_MGMT_1);
  Wire.write(0x00);  
  Wire.endTransmission();
  delay(100);

  Serial.println("================================");
  Serial.println("ACCEL ONLY ANGLE MONITOR START");
  Serial.println("================================");
  Serial.println();
}

void loop() {
  float ax, ay, az;
  
  if (readMPU6500Accel(ax, ay, az)) {
    // 1. 가속도계 기반 Y축 절대 각도 계산 (단위: 도)
    float rawAccAngleY = atan2(ax, sqrt(ay * ay + az * az)) * 180.0 / PI;

    // 2. EMA(지수 이동 평균) 필터 적용 (운동 중 순간적인 휨/충격 노이즈 감쇄)
    filteredAngleY = (alpha * rawAccAngleY) + ((1.0 - alpha) * filteredAngleY);

    // 3. 시리얼 모니터 출력
    Serial.print("Raw Angle : ");
    Serial.print(rawAccAngleY, 1);
    Serial.print(" deg   |   Filtered Angle : ");
    Serial.print(filteredAngleY, 1);
    Serial.println(" deg");
  } else {
    Serial.println("MPU6500 I2C ERROR");
  }

  delay(20); // 약 50Hz 주기
}
