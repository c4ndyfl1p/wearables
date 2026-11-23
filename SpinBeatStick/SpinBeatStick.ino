#include <WiFi.h>
#include <esp_now.h>
#include "soc/soc.h"
#include "soc/rtc_io_reg.h"
#include "samples.h"

// ================= SETTINGS =================
const int   SPIN_AXIS = 2;          // gyro axis the stick spins around: 0 = x, 1 = y, 2 = z
const float DEAD_ZONE = 0.5f;       // rad/s (~30 deg/s): slower rotation is ignored as noise
const float FULL_SPIN = 2.0f * PI;  // one full spin = 360 degrees = 2*pi radians
const int   VOLUME = 40;            // 0-256


// ================= SOUND =================
// The sound that is playing right now
const int8_t* soundData = nullptr;  // the sound's numbers (from samples.h)
uint32_t soundLength = 0;           // how many numbers the sound has
volatile uint32_t soundPos = 0;     // which number we are at
volatile bool soundPlaying = false;

hw_timer_t* timer = nullptr;

// Runs 22,000 times per second: sends the next number of the sound to the speaker
void ARDUINO_ISR_ATTR onTimer() {
  int value = 0;
  if (soundPlaying) {
    if (soundPos < soundLength) {
      value = soundData[soundPos];
      soundPos++;
    } else {
      soundPlaying = false;         // reached the end of the sound
    }
  }
  value = (value * VOLUME) / 256 + 128;   // apply volume, center around 128
  SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, (uint32_t)value, RTC_IO_PDAC1_DAC_S);
}

// Starts a sound from the beginning
void playSound(const int8_t* data, uint32_t length) {
  soundPlaying = false;             // pause while we switch sounds
  soundData = data;
  soundLength = length;
  soundPos = 0;
  soundPlaying = true;
}


// ================= SPIN DETECTION =================
float angle = 0;                    // how far the stick has turned since the last spin (radians)
unsigned long lastTime = 0;         // the stick's timestamp from the previous message
int spinCount = 0;

// Give it the rotation speed and the time of the message.
// Returns +1 when a spin is finished in one direction,
//         -1 when a spin is finished in the other direction,
//          0 when no spin was finished yet.
int detectSpin(float rate, unsigned long time) {
  // First message, stick restarted, or long gap: start fresh
  if (lastTime == 0 || time < lastTime || time - lastTime > 500) {
    lastTime = time;
    return 0;
  }

  float seconds = (time - lastTime) / 1000.0f;
  lastTime = time;

  if (fabsf(rate) < DEAD_ZONE) rate = 0;   // ignore tiny movements
  if (rate * angle < 0) angle = 0;          // direction changed: start counting fresh

  angle = angle + rate * seconds;           // speed x time = how far it turned

  if (angle >= FULL_SPIN) {
    angle = angle - FULL_SPIN;
    return 1;
  }
  if (angle <= -FULL_SPIN) {
    angle = angle + FULL_SPIN;
    return -1;
  }
  return 0;
}


// ================= RECEIVING =================
// Runs every time a message from the stick arrives.
// Message: time(ms),accelX,accelY,accelZ,gyroX,gyroY,gyroZ   (gyro in rad/s)
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  char text[128];
  if (len >= 128) return;
  memcpy(text, data, len);
  text[len] = '\0';

  // Read the 7 numbers out of the text
  unsigned long time;
  float ax, ay, az, gx, gy, gz;
  char* p = text;
  time = strtoul(p, &p, 10);  p++;
  ax = strtof(p, &p);  p++;
  ay = strtof(p, &p);  p++;
  az = strtof(p, &p);  p++;
  gx = strtof(p, &p);  p++;
  gy = strtof(p, &p);  p++;
  gz = strtof(p, &p);

  // Pick the axis the stick spins around
  float rate = gz;
  if (SPIN_AXIS == 0) rate = gx;
  if (SPIN_AXIS == 1) rate = gy;

  int spin = detectSpin(rate, time);

  if (spin == 1) {
    spinCount++;
    Serial.printf("spin detected (+ direction)   total: %d\n", spinCount);
    playSound(kick_data, kick_len);
  }
  if (spin == -1) {
    spinCount++;
    Serial.printf("spin detected (- direction)   total: %d\n", spinCount);
    playSound(snare_data, snare_len);
  }
}


// ================= SETUP & LOOP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Sound
  dacWrite(25, 128);                 // turn the DAC on (128 = silence)
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 45, true, 0);    // every 45 microseconds = 22,000 times per second

  // Radio
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed to start");
    while (1) delay(10);
  }
  esp_now_register_recv_cb(onReceive);

  Serial.println("Ready. Spin the stick!");
}

void loop() {
  // nothing here - everything happens in onReceive()
}
