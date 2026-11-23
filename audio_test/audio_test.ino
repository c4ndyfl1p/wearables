// AudioTest.ino — finds out which part of the audio chain isn't working
// Wiring: same as before (D25 -> PAM8403 L input)

#include "soc/soc.h"
#include "soc/rtc_io_reg.h"

volatile uint32_t isrCount = 0;
volatile bool useTimer = false;
hw_timer_t* timer = nullptr;

void ARDUINO_ISR_ATTR onTimer() {
  isrCount++;
  if (!useTimer) return;
  // 440 Hz square wave: flip every 25 interrupts (22,222 per second / 440 / 2)
  static uint32_t n = 0;
  static bool high = false;
  if (++n >= 25) {
    n = 0;
    high = !high;
    SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, high ? 200 : 55, RTC_IO_PDAC1_DAC_S);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Audio test start ===");
  dacWrite(25, 128);

  timer = timerBegin(1000000);
  if (timer == nullptr) Serial.println("ERROR: timerBegin failed");
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 45, true, 0);
}

void loop() {
  // Test A: the method from your working test tone
  Serial.println("Test A: dacWrite tone (2 seconds)");
  useTimer = false;
  unsigned long t0 = millis();
  while (millis() - t0 < 2000) {
    dacWrite(25, 200); delayMicroseconds(1136);
    dacWrite(25, 55);  delayMicroseconds(1136);
  }
  dacWrite(25, 128);

  // Test B: the method SpinSamples uses
  Serial.println("Test B: timer interrupt tone (2 seconds)");
  uint32_t c0 = isrCount;
  useTimer = true;
  delay(2000);
  useTimer = false;
  Serial.printf("Timer fired %lu times in 2 s (should be about 44000)\n\n",
                (unsigned long)(isrCount - c0));

  delay(1000);
}
