// SpinSamples.ino — sample-based beat engine for the ESP32 (DOIT DevKit V1)
// Audio out: GPIO 25 (D25, built-in DAC) -> amplifier -> speaker
// Sounds live in samples.h (make your own with wav2header.py)
//
// Serial Monitor (115200 baud) controls:
//   0 1 2 3   spin speed: stopped / slow / medium / fast
//   v h       spin plane: vertical (section A) / horizontal (section B)
//   t c       toss / catch
//   r         direction reversal (crash)
// Full commands (used later by the staff over ESP-NOW):
//   S <speed_deg_per_s> <plane>    E REV    E TOSS    E CATCH

#include "soc/soc.h"
#include "soc/rtc_io_reg.h"
#include "samples.h"

// ---------------- Settings ----------------
const float BPM = 110.0f;
const float LEVEL_UP[4] = {0, 150, 400, 700};   // deg/s needed for level 1, 2, 3
const float HYST = 80;
const int   MASTER = 180;                       // overall volume, 0-256
const int   BASS_SAMPLE_NOTE = 57;              // note your bass.wav is played at (57 = A3)
const int   PAD_SAMPLE_NOTE  = 57;              // root note of your pad.wav

// ---------------- Patterns ----------------
const char* KICK   = "x...x...x...x...";
const char* SNARE  = "....x.......x...";
const char* HAT    = "x.x.x.x.x.x.x.x.";
const char* BASS_A = "x..x..x.x..x..x.";
const char* BASS_B = "x.x.x.x.x.x.x.xx";
const int ROOTS_A[4] = {57, 53, 60, 55};        // A F C G
const int ROOTS_B[4] = {50, 53, 55, 57};        // D F G A

// ---------------- Sample player (runs in a timer interrupt, 22 kHz) ----------------
enum { V_KICK, V_SNARE, V_HAT, V_CRASH, V_BASS, V_PAD, V_RISER, NUM_VOICES };

struct Voice {
  const int8_t* data = nullptr;
  uint32_t len = 0;       // length in samples
  uint32_t pos = 0;       // playback position, 16.16 fixed point
  uint32_t inc = 65536;   // playback speed (65536 = normal, higher = higher pitch)
  int32_t  gain = 0;      // 0-256
  bool     loop = false;
  bool     active = false;
};

Voice voices[NUM_VOICES];
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t* timer = nullptr;

void ARDUINO_ISR_ATTR onTimer() {
  int32_t mix = 0;
  portENTER_CRITICAL_ISR(&mux);
  for (int i = 0; i < NUM_VOICES; i++) {
    Voice& v = voices[i];
    if (!v.active) continue;
    uint32_t idx = v.pos >> 16;
    if (idx >= v.len) {
      if (v.loop) { v.pos -= v.len << 16; idx = v.pos >> 16; }
      else        { v.active = false; continue; }
    }
    mix += v.data[idx] * v.gain;
    v.pos += v.inc;
  }
  portEXIT_CRITICAL_ISR(&mux);

  mix = (mix * MASTER) >> 16;
  if (mix > 127)  mix = 127;
  if (mix < -128) mix = -128;
  // Same DAC as dacWrite(25, ...), written directly because this runs 22,000 times a second
  SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, (uint32_t)(mix + 128), RTC_IO_PDAC1_DAC_S);
}

void play(int vi, const int8_t* data, uint32_t len, int gain, float rate = 1.0f, bool loop = false) {
  uint32_t inc = (uint32_t)(rate * 65536.0f);
  portENTER_CRITICAL(&mux);
  Voice& v = voices[vi];
  v.data = data; v.len = len; v.pos = 0; v.inc = inc;
  v.gain = gain; v.loop = loop; v.active = true;
  portEXIT_CRITICAL(&mux);
}

void setGain(int vi, int gain) {
  portENTER_CRITICAL(&mux); voices[vi].gain = gain; portEXIT_CRITICAL(&mux);
}

void setRate(int vi, float rate) {
  uint32_t inc = (uint32_t)(rate * 65536.0f);
  portENTER_CRITICAL(&mux); voices[vi].inc = inc; portEXIT_CRITICAL(&mux);
}

void stopVoice(int vi) {
  portENTER_CRITICAL(&mux); voices[vi].active = false; portEXIT_CRITICAL(&mux);
}

float semitones(int n) { return powf(2.0f, n / 12.0f); }   // pitch shift as speed ratio

// ---------------- Control state ----------------
float rawSpeed = 0, smoothSpeed = 0, padGain = 0;
int   rawPlane = 0, planeCandidate = 0;
unsigned long planeSince = 0;
int   level = 0, section = 0, requestedSection = 0;
int   step = 0, bar = -1, currentRoot = 57;
bool  dropMode = false, pendingCrash = false, pendingKick = false;

void toss()  { if (!dropMode) { dropMode = true; play(V_RISER, riser_data, riser_len, 200); } }
void catchStaff() {
  if (dropMode) {
    dropMode = false;
    stopVoice(V_RISER);
    pendingCrash = true; pendingKick = true;
    step = 0;                                   // drop lands on the next downbeat
  }
}

// ---------------- Sequencer ----------------
void playStep() {
  if (step == 0) {
    bar = (bar + 1) % 4;
    section = requestedSection;
    currentRoot = (section == 0 ? ROOTS_A : ROOTS_B)[bar];
    setRate(V_PAD, semitones(currentRoot - PAD_SAMPLE_NOTE));
  }

  if (pendingCrash) { play(V_CRASH, crash_data, crash_len, 160); pendingCrash = false; }

  bool kickNow = pendingKick || (!dropMode && level >= 1 && KICK[step] == 'x');
  pendingKick = false;
  if (kickNow) play(V_KICK, kick_data, kick_len, 256);

  if (!dropMode) {
    if (level >= 2 && SNARE[step] == 'x') play(V_SNARE, snare_data, snare_len, 220);
    if (level >= 2) {
      const char* pat = (section == 0) ? BASS_A : BASS_B;
      if (pat[step] == 'x') {
        int octave = (section == 1 && step % 4 == 2) ? 12 : 0;
        play(V_BASS, bass_data, bass_len, 200, semitones(currentRoot + octave - BASS_SAMPLE_NOTE));
      }
    }
    if (level >= 3 && HAT[step] == 'x')
      play(V_HAT, hat_data, hat_len, step % 4 == 2 ? 200 : 110);
  }

  step = (step + 1) % 16;
}

void updateControls() {                         // every 10 ms
  smoothSpeed += 0.15f * (rawSpeed - smoothSpeed);
  while (level < 3 && smoothSpeed > LEVEL_UP[level + 1]) level++;
  while (level > 0 && smoothSpeed < LEVEL_UP[level] - HYST) level--;

  if (rawPlane != planeCandidate) { planeCandidate = rawPlane; planeSince = millis(); }
  else if (millis() - planeSince > 300) requestedSection = planeCandidate;

  float padTarget = (level == 0) ? 130 : 70;    // pad louder when the staff is still
  padGain += 0.05f * (padTarget - padGain);
  setGain(V_PAD, (int)padGain);
}

// ---------------- Serial input ----------------
void handleCommand(String s) {
  s.trim();
  if (s.startsWith("S ")) {
    String rest = s.substring(2);
    int sp = rest.indexOf(' ');
    rawSpeed = fabsf(rest.toFloat());
    if (sp > 0) rawPlane = rest.substring(sp + 1).toInt();
  }
  else if (s == "E REV")   pendingCrash = true;
  else if (s == "E TOSS")  toss();
  else if (s == "E CATCH") catchStaff();
}

bool handleShortcut(char c) {
  switch (c) {
    case '0': rawSpeed = 0;   return true;
    case '1': rawSpeed = 250; return true;
    case '2': rawSpeed = 550; return true;
    case '3': rawSpeed = 850; return true;
    case 'v': rawPlane = 0;   return true;
    case 'h': rawPlane = 1;   return true;
    case 't': toss();         return true;
    case 'c': catchStaff();   return true;
    case 'r': pendingCrash = true; return true;
  }
  return false;
}

// ---------------- Setup & loop ----------------
const unsigned long STEP_US = (unsigned long)(60000000.0f / BPM / 4);
unsigned long lastStep = 0, lastCtrl = 0, lastPrint = 0;
String lineBuf;

void setup() {
  Serial.begin(115200);
  dacWrite(25, 128);                            // switch the DAC on, at the middle level

  play(V_PAD, pad_data, pad_len, 0, 1.0f, true); // pad loops forever; volume set by speed

  timer = timerBegin(1000000);                  // 1 MHz timer clock
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 45, true, 0);               // every 45 us = ~22 kHz

  lastStep = micros();
  Serial.println("SpinSamples ready.");
  Serial.println("Speed: 0 1 2 3   Plane: v h   Toss/catch: t c   Reversal: r");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (lineBuf.length() == 0 && handleShortcut(c)) continue;
    if (c == '\n' || c == '\r') {
      if (lineBuf.length()) handleCommand(lineBuf);
      lineBuf = "";
    } else {
      lineBuf += c;
    }
  }

  unsigned long now = micros();
  if (now - lastStep >= STEP_US) { lastStep += STEP_US; playStep(); }

  if (millis() - lastCtrl >= 10) { lastCtrl = millis(); updateControls(); }

  if (millis() - lastPrint >= 500) {
    lastPrint = millis();
    Serial.printf("speed %d  level %d  section %c %s\n",
                  (int)rawSpeed, level, section ? 'B' : 'A', dropMode ? " [TOSS]" : "");
  }
}
