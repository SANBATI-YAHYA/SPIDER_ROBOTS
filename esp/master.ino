#include <Adafruit_PWMServoDriver.h>
#include <Wire.h>

#define I2C_SDA 21
#define I2C_SCL 22

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

#define SERVOMIN 150
#define SERVOMAX 600
#define SERVO_FREQ 60

void setup() {
  Serial.begin(115200);
  Serial.println(
      "--- Calibration Mode: Even Ports (0, 2, 4... 14) to 90 Deg ---");

  Wire.begin(I2C_SDA, I2C_SCL);
  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);

  delay(500);
}

void loop() {
  int pulse90 = map(90, 0, 180, SERVOMIN, SERVOMAX);

  Serial.println("Holding 90 degrees on ports: 0, 2, 4, 6, 8, 10, 12, 14");

  // الدورة هنا كتزيد بـ 2 كل مرة (i += 2)
  for (int i = 0; i <= 14; i += 2) {
    pwm.setPWM(i, 0, pulse90);
  }

  delay(1000);
}