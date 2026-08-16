#include <Wire.h>

#define MPU6500_ADDR 0x68
#define SDA_PIN 8 
#define SCL_PIN 9 

#define PWR_MGMT_1    0x6B
#define ACCEL_XOUT_H  0x3B

float filteredAngleY = 0;
const float alpha = 0.15; // EMA 필터 계수

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
  
  ax = axRaw / 16384.0;
  ay = ayRaw / 16384.0;
  az = azRaw / 16384.0;
  
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);  
  delay(100);

  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(PWR_MGMT_1);
  Wire.write(0x00);  
  Wire.endTransmission();
  delay(100);
}

void loop() {
  float ax, ay, az;
  
  if (readMPU6500Accel(ax, ay, az)) {
    // 1. 가속도계 기반 Y축 절대 각도 계산
    float rawAccAngleY = atan2(ax, sqrt(ay * ay + az * az)) * 180.0 / PI;

    // 2. EMA 필터 적용
    filteredAngleY = (alpha * rawAccAngleY) + ((1.0 - alpha) * filteredAngleY);

    // 3. 시리얼 플로터 전용 출력 방식 ("라벨:값, 라벨2:값2")
    Serial.print("Raw_Angle:");
    Serial.print(rawAccAngleY, 1);
    Serial.print(",");
    Serial.print("Filtered_Angle:");
    Serial.println(filteredAngleY, 1);
  }

  delay(20); // 50Hz
}