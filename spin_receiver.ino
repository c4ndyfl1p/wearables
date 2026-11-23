#include <WiFi.h>
#include <esp_now.h>

// ---------------- Settings ----------------
const int   SPIN_AXIS = 2;          // gyro axis the staff spins around: 0 = x, 1 = y, 2 = z
const float DEAD_ZONE = 0.5f;       // rad/s (~30 deg/s): slower rotation is ignored as noise
const float FULL_SPIN = 2.0f * PI;  // one full spin = 360 degrees = 2*pi radians

// ---------------- Spin tracking ----------------
float angle = 0;                    // how far the stick has turned since the last spin (radians)
unsigned long lastTime = 0;         // the stick's timestamp from the previous message
uint32_t spinCount = 0;

// Runs every time a message from the stick arrives.
// Message format: time(ms),accelX,accelY,accelZ,gyroX,gyroY,gyroZ   (gyro in rad/s)
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  char buf[128];
  if (len >= (int)sizeof(buf)) return;
  memcpy(buf, data, len);
  buf[len] = '\0';                  // turn the bytes into a proper text string

  // Split the text at the commas
  char *p = buf;
  unsigned long t = strtoul(p, &p, 10);
  float v[6];                       // ax, ay, az, gx, gy, gz
  for (int i = 0; i < 6; i++) {
    if (*p != ',') return;          // badly formed message: skip it
    v[i] = strtof(p + 1, &p);
  }
  float rate = v[3 + SPIN_AXIS];    // rotation speed around the chosen axis

  // Time since the previous message, using the stick's own clock
  if (lastTime == 0 || t < lastTime || t - lastTime > 500) {
    lastTime = t;                   // first message, stick restarted, or long gap: start fresh
    return;
  }
  float dt = (t - lastTime) / 1000.0f;
  lastTime = t;

  if (fabsf(rate) < DEAD_ZONE) rate = 0;   // ignore tiny movements and sensor drift
  if (rate * angle < 0) angle = 0;          // direction changed: start counting fresh

  angle += rate * dt;                       // speed x time = how far it turned

  if (angle >= FULL_SPIN) {
    angle -= FULL_SPIN;
    spinCount++;
    Serial.printf("spin detected (+ direction)   total: %lu\n", (unsigned long)spinCount);
  } else if (angle <= -FULL_SPIN) {
    angle += FULL_SPIN;
    spinCount++;
    Serial.printf("spin detected (- direction)   total: %lu\n", (unsigned long)spinCount);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);       // turn on the radio
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed to start");
    while (1) delay(10);
  }
  esp_now_register_recv_cb(onReceive);

  Serial.printf("Spin detector ready. Watching gyro axis %c.\n", "xyz"[SPIN_AXIS]);
}

void loop() {
  // nothing here - everything happens in onReceive()
}
