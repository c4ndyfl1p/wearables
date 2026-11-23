// BeatPlayer.h — plays sounds from samples.h through the DAC on GPIO 25 (D25).
//
// How to use:
//   beatPlayerBegin();            in setup()
//   playSound(SOUND(kick));       play any sound from samples.h, by name
//   setVolume(40);                0-256 (keep it fairly high and turn the amp down
//                                 for the cleanest sound)
//
// Up to 6 sounds can overlap; if more start, the oldest is replaced.

#pragma once
#include "soc/soc.h"
#include "soc/rtc_io_reg.h"

#define SOUND(name) name##_data, name##_len

namespace BeatPlayerInternal {
  const int VOICES = 6;
  struct Voice { const int8_t* data; uint32_t len; uint32_t pos; bool active; };

  Voice voices[VOICES];
  int nextVoice = 0;
  volatile int master = 40;
  int8_t limiter[1024];
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  hw_timer_t* timer = nullptr;

  // Runs ~22,000 times per second: mixes all playing sounds into one DAC value
  void ARDUINO_ISR_ATTR onTimer() {
    int32_t mix = 0;
    portENTER_CRITICAL_ISR(&mux);
    for (int i = 0; i < VOICES; i++) {
      Voice& v = voices[i];
      if (!v.active) continue;
      if (v.pos >= v.len) { v.active = false; continue; }
      mix += v.data[v.pos++];
    }
    portEXIT_CRITICAL_ISR(&mux);

    mix = (mix * master) >> 8;
    if (mix > 511)  mix = 511;
    if (mix < -512) mix = -512;
    mix = limiter[mix + 512];
    SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, (uint32_t)(mix + 128), RTC_IO_PDAC1_DAC_S);
  }
}

void beatPlayerBegin() {
  using namespace BeatPlayerInternal;
  dacWrite(25, 128);
  for (int i = 0; i < 1024; i++)
    limiter[i] = (int8_t)roundf(127.0f * tanhf((i - 512) / 128.0f));
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 45, true, 0);
}

void playSound(const int8_t* data, uint32_t len) {
  using namespace BeatPlayerInternal;
  portENTER_CRITICAL(&mux);
  int slot = -1;
  for (int i = 0; i < VOICES; i++)
    if (!voices[i].active) { slot = i; break; }
  if (slot < 0) { slot = nextVoice; nextVoice = (nextVoice + 1) % VOICES; }
  voices[slot].data = data;
  voices[slot].len = len;
  voices[slot].pos = 0;
  voices[slot].active = true;
  portEXIT_CRITICAL(&mux);
}

void setVolume(int volume) {
  BeatPlayerInternal::master = constrain(volume, 0, 256);
}
