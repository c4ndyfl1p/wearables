#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <esp_system.h>

// Send to every ESP32 nearby (the receiver picks it up)
uint8_t receiverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

Adafruit_MPU6050 mpu;
// cus i dont have more usb ports
const int SENSOR_ID = 3; // new: 1 for the first C3, 2 for the second
static_assert(SENSOR_ID >= 1 && SENSOR_ID <= 4, "SENSOR_ID must be unique and in range 1-4");

void setup()
{
  Serial.begin(115200);
  delay(1000);

  // Sensor
  // 3, 2 for the blue ones
  // 20, 21 for the other ones
  Wire.begin(20, 21);
  if (!mpu.begin())
  {
    Serial.println("Failed to find MPU6050 chip");
    while (1)
      delay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
  Serial.println("MPU6050 Found!");

  // Radio
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm); // fixes a known radio issue on C3 SuperMini
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW failed to start");
    while (1)
      delay(10);
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverAddress, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.println("Sender ready. Sending stick data...");
}

void loop()
{
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // id, time(ms), accelX, accelY, accelZ, gyroX, gyroY, gyroZ
  char msg[100];
  int len = snprintf(msg, sizeof(msg), "%d,%lu,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f", // NEW: %d, at the start
                     SENSOR_ID,                                                // NEW
                     millis(),
                     a.acceleration.x, a.acceleration.y, a.acceleration.z,
                     g.gyro.x, g.gyro.y, g.gyro.z);

  esp_now_send(receiverAddress, (uint8_t *)msg, len); // send through the air

  // Small independent timing jitter prevents identical 50 Hz loops on
  // multiple clients from repeatedly transmitting at the same instant.
  delay(18 + (esp_random() % 5));
}
