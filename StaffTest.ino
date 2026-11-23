// StaffTest.ino — ESP32-C3 SuperMini + MPU-6050 (GY-521) sensor test
// Wiring: VCC -> 3V3, GND -> GND, SDA -> GPIO 6, SCL -> GPIO 7
// Arduino IDE: board "ESP32C3 Dev Module", Tools -> USB CDC On Boot -> Enabled

#include <Wire.h>

const int SDA_PIN = 6;
const int SCL_PIN = 7;
int mpuAddr = 0x68;            // 0x69 on some boards; found automatically below

void writeReg(int reg, int val) {
  Wire.beginTransmission(mpuAddr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool found(int addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

int16_t read16() {
  int16_t hi = Wire.read();
  int16_t lo = Wire.read();
  return (hi << 8) | lo;
}

void setup() {
  Serial.begin(115200);
  delay(1500);                 // give the USB serial a moment to connect
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);       // fast I2C

  if (found(0x68))      mpuAddr = 0x68;
  else if (found(0x69)) mpuAddr = 0x69;
  else {
    Serial.println("MPU-6050 NOT found. Check the four wires.");
    while (true) delay(1000);
  }
  Serial.printf("MPU-6050 found at 0x%X\n", mpuAddr);

  writeReg(0x6B, 0x00);        // wake the sensor up (it starts asleep)
  writeReg(0x1A, 0x03);        // built-in smoothing filter, about 44 Hz
  writeReg(0x1B, 0x18);        // gyro range: +/- 2000 degrees per second
  writeReg(0x1C, 0x18);        // accelerometer range: +/- 16 g
}

void loop() {
  // Ask for 14 bytes starting at register 0x3B: accel XYZ, temperature, gyro XYZ
  Wire.beginTransmission(mpuAddr);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(mpuAddr, 14);
  if (Wire.available() < 14) return;

  float ax = read16() / 2048.0f;   // in g (2048 per g at +/- 16 g)
  float ay = read16() / 2048.0f;
  float az = read16() / 2048.0f;
  read16();                        // temperature, not needed
  float gx = read16() / 16.4f;     // in degrees/second (16.4 per deg/s at +/- 2000)
  float gy = read16() / 16.4f;
  float gz = read16() / 16.4f;

  float g = sqrtf(ax * ax + ay * ay + az * az);   // total acceleration

  Serial.printf("gyro  x %6.0f  y %6.0f  z %6.0f deg/s   |   accel %.2f g\n", gx, gy, gz, g);
  delay(100);
}
