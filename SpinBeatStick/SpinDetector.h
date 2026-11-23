// SpinDetector.h — turns stick data received over ESP-NOW into spin events.
//
// How to use:
//   SpinDetector spins;
//   void onSpin(int direction) { ... }   // direction is +1 or -1
//   setup():  spins.begin(onSpin);
//   loop():   spins.update();             // your onSpin() gets called from here
//
// Extras:
//   spins.rate()       current rotation speed in degrees/second (+ or -)
//   spins.count()      total spins so far
//   spins.connected()  true if data arrived from the stick in the last half second
//
// Expects the stick to send text: time(ms),ax,ay,az,gx,gy,gz  (gyro in rad/s)

#pragma once
#include <WiFi.h>
#include <esp_now.h>

static portMUX_TYPE spinLock = portMUX_INITIALIZER_UNLOCKED;

class SpinDetector {
public:
  typedef void (*Callback)(int direction);

  // ---- Settings: change these before calling begin() if needed ----
  int   axis = 2;                 // gyro axis the stick spins around: 0 = x, 1 = y, 2 = z
  float deadZone = 0.5f;          // rad/s (~30 deg/s): slower rotation is ignored as noise
  float degreesPerSpin = 360.0f;  // 360 = event every full spin, 180 = every half spin

  // Starts listening for the stick. Returns false if ESP-NOW couldn't start.
  bool begin(Callback cb) {
    callback = cb;
    instance = this;
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(onReceiveStatic);
    return true;
  }

  // Call this in loop(). Runs your callback once for every spin since the last call.
  void update() {
    portENTER_CRITICAL(&spinLock);
    int pos = pendingPos, neg = pendingNeg;
    pendingPos = pendingNeg = 0;
    portEXIT_CRITICAL(&spinLock);

    if (!callback) return;
    for (int i = 0; i < pos; i++) callback(+1);
    for (int i = 0; i < neg; i++) callback(-1);
  }

  float    rate()      { return rateDeg; }
  uint32_t count()     { return total; }
  bool     connected() { return millis() - lastRxMs < 500; }

private:
  static SpinDetector* instance;
  Callback callback = nullptr;

  volatile int      pendingPos = 0, pendingNeg = 0;   // spins waiting for update()
  volatile float    rateDeg = 0;
  volatile uint32_t total = 0;
  volatile unsigned long lastRxMs = 0;

  float angle = 0;                // radians turned since the last spin
  unsigned long lastTime = 0;     // stick's timestamp of the previous message

  static void onReceiveStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (instance) instance->handle(data, len);
  }

  // Runs inside the Wi-Fi system for every message: kept short, no printing.
  void handle(const uint8_t* data, int len) {
    char buf[128];
    if (len >= (int)sizeof(buf)) return;
    memcpy(buf, data, len);
    buf[len] = '\0';

    char* p = buf;
    unsigned long t = strtoul(p, &p, 10);
    float v[6];                               // ax, ay, az, gx, gy, gz
    for (int i = 0; i < 6; i++) {
      if (*p != ',') return;
      v[i] = strtof(p + 1, &p);
    }
    float rate = v[3 + axis];
    rateDeg = rate * RAD_TO_DEG;
    lastRxMs = millis();

    if (lastTime == 0 || t < lastTime || t - lastTime > 500) {
      lastTime = t;                           // first message, restart, or long gap
      return;
    }
    float dt = (t - lastTime) / 1000.0f;
    lastTime = t;

    if (fabsf(rate) < deadZone) rate = 0;
    if (rate * angle < 0) angle = 0;          // direction changed: count from zero

    angle += rate * dt;
    float spinAngle = degreesPerSpin * DEG_TO_RAD;

    if (angle >= spinAngle) {
      angle -= spinAngle;
      portENTER_CRITICAL(&spinLock); pendingPos++; total++; portEXIT_CRITICAL(&spinLock);
    } else if (angle <= -spinAngle) {
      angle += spinAngle;
      portENTER_CRITICAL(&spinLock); pendingNeg++; total++; portEXIT_CRITICAL(&spinLock);
    }
  }
};

SpinDetector* SpinDetector::instance = nullptr;
