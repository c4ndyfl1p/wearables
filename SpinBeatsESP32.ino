// SpinBeatsESP32.ino — beat engine for the original ESP32
// Audio out: GPIO 25 (built-in DAC) -> amplifier -> speaker
// Requires the ESP32 Arduino core version 3.x ("esp32 by Espressif Systems")
//
// Control it from the Serial Monitor (115200 baud):
//   0 1 2 3   spin speed: stopped / slow / medium / fast
//   v h       spin plane: vertical (section A) / horizontal (section B)
//   t c       toss / catch
//   r         direction reversal (crash)
// Full commands (used later by the staff over ESP-NOW):
//   S <speed_deg_per_s> <plane>    E REV    E TOSS    E CATCH

#include "driver/dac_continuous.h"

// ---------------- Settings ----------------
const int   SR = 22050;                         // sample rate (samples per second)
const float BPM = 110.0f;
const float LEVEL_UP[4] = {0, 150, 400, 700};   // deg/s needed for level 1, 2, 3
const float HYST = 80;                          // drop this far below to go down a level
const float MASTER_VOL = 0.8f;
const int   BASS_TRANSPOSE = 12;                // bass up an octave for small speakers
const int   BLOCK = 256;                        // samples rendered at a time

// ---------------- Patterns ----------------
const char* KICK   = "x...x...x...x...";
const char* SNARE  = "....x.......x...";
const char* HAT    = "x.x.x.x.x.x.x.x.";
const char* BASS_A = "x..x..x.x..x..x.";
const char* BASS_B = "x.x.x.x.x.x.x.xx";
const int ROOTS_A[4] = {45, 41, 48, 43};        // A F C G  (MIDI note numbers)
const int ROOTS_B[4] = {38, 41, 43, 45};        // D F G A

// ---------------- Shared state (loop writes, audio task reads) ----------------
volatile float rawSpeed = 0;
volatile int   rawPlane = 0;
volatile bool  evRev = false, evToss = false, evCatch = false;
volatile int   dispLevel = 0, dispSection = 0;
volatile bool  dispToss = false;

// ---------------- Helpers ----------------
static uint32_t rng = 22222;
inline float noise() {                          // fast white noise, -1..1
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return (int32_t)rng * (1.0f / 2147483648.0f);
}
float decayCoef(float seconds) { return expf(-1.0f / (seconds * SR)); }
float midiToHz(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

struct SVF {                                    // simple resonant filter
  float low = 0, band = 0, f = 0.1f, q = 1.0f;
  void setFreq(float hz) { f = 2.0f * sinf(PI * hz / SR); }
  void process(float in) {
    low += f * band;
    float high = in - low - q * band;
    band += f * high;
  }
};

// ---------------- Voices ----------------
float kickAmp = 0, kickFreq = 0, kickPhase = 0, kickDec;
float snNoiseAmp = 0, snToneAmp = 0, snPhase = 0, snNoiseDec, snToneDec;
float hatAmp = 0, hatDec;
float crAmp = 0, crDec;
float bassAmp = 0, bassPhase = 0, bassFreq = 110, bassLP = 0, bassDec;
float padPh1 = 0, padPh2 = 0, padF1 = 220, padF2 = 330, padGain = 0;
SVF   padFilt, riserFilt;
float riserGain = 0, prevNoise = 0;

void trigKick()          { kickAmp = 1.0f; kickFreq = 220; }
void trigSnare()         { snNoiseAmp = 0.7f; snToneAmp = 0.6f; }
void trigHat(float a)    { hatAmp = a; }
void trigCrash()         { crAmp = 0.6f; }
void trigBass(int midi)  { bassFreq = midiToHz(midi); bassAmp = 0.7f; }

// ---------------- Sequencer state (audio task only) ----------------
float smoothSpeed = 0, samplesPerStep, stepCounter = 0;
int   level = 0, section = 0, requestedSection = 0;
int   planeCandidate = 0, planeStableBlocks = 0;
int   step = 0, bar = -1, currentRoot = 45;
bool  dropMode = false, pendingCrash = false, pendingKick = false;
uint32_t tossSamples = 0;

void playStep() {
  if (step == 0) {
    bar = (bar + 1) % 4;
    section = requestedSection;                 // sections only change on a new bar
    currentRoot = (section == 0 ? ROOTS_A : ROOTS_B)[bar];
    padF1 = midiToHz(currentRoot + 12);
    padF2 = midiToHz(currentRoot + 19);         // a fifth above
  }

  if (pendingCrash) { trigCrash(); pendingCrash = false; }

  bool kickNow = pendingKick || (!dropMode && level >= 1 && KICK[step] == 'x');
  pendingKick = false;
  if (kickNow) trigKick();

  if (!dropMode) {
    if (level >= 2 && SNARE[step] == 'x') trigSnare();
    if (level >= 2) {
      const char* pat = (section == 0) ? BASS_A : BASS_B;
      if (pat[step] == 'x') {
        int octave = (section == 1 && step % 4 == 2) ? 12 : 0;
        trigBass(currentRoot + BASS_TRANSPOSE + octave);
      }
    }
    if (level >= 3 && HAT[step] == 'x') trigHat(step % 4 == 2 ? 0.5f : 0.25f);
  }

  step = (step + 1) % 16;
}

// Runs once per block (~12 ms)
void updateControls() {
  if (evRev)   { evRev = false; pendingCrash = true; }
  if (evToss)  { evToss = false; if (!dropMode) { dropMode = true; tossSamples = 0; } }
  if (evCatch) {
    evCatch = false;
    if (dropMode) { dropMode = false; pendingCrash = true; pendingKick = true; step = 0; }
  }

  smoothSpeed += 0.15f * (rawSpeed - smoothSpeed);
  while (level < 3 && smoothSpeed > LEVEL_UP[level + 1]) level++;
  while (level > 0 && smoothSpeed < LEVEL_UP[level] - HYST) level--;

  int pl = rawPlane;
  if (pl != planeCandidate) { planeCandidate = pl; planeStableBlocks = 0; }
  else if (++planeStableBlocks > 25) requestedSection = planeCandidate;   // stable ~300 ms

  // Continuous layer: faster spinning opens up the pad
  padFilt.setFreq(fminf(fmaxf(300 + smoothSpeed * 3, 300), 3000));
  float padTarget = (level == 0) ? 0.35f : 0.18f;
  padGain += 0.03f * (padTarget - padGain);

  // Riser while the staff is in the air
  if (dropMode) {
    float p = fminf(tossSamples / (2.0f * SR), 1.0f);
    riserFilt.setFreq(250 * powf(14, p));
    riserGain = 0.05f + 0.4f * p;
  } else {
    riserGain = 0;
  }

  dispLevel = level; dispSection = section; dispToss = dropMode;
}

// Produces one audio sample, -1..1
float renderSample() {
  stepCounter += 1.0f;
  if (stepCounter >= samplesPerStep) { stepCounter -= samplesPerStep; playStep(); }
  if (dropMode) tossSamples++;

  float n = noise();
  float hp = (n - prevNoise) * 0.5f;            // brighter noise for snare/hats/crash
  prevNoise = n;
  float out = 0;

  if (kickAmp > 0.001f) {                       // kick: sine with falling pitch
    kickFreq = 70 + (kickFreq - 70) * 0.9992f;
    kickPhase += kickFreq / SR; if (kickPhase >= 1) kickPhase -= 1;
    out += sinf(TWO_PI * kickPhase) * kickAmp;
    kickAmp *= kickDec;
  }
  if (snNoiseAmp > 0.001f || snToneAmp > 0.001f) {   // snare: noise + tone
    snPhase += 190.0f / SR; if (snPhase >= 1) snPhase -= 1;
    out += hp * snNoiseAmp + sinf(TWO_PI * snPhase) * snToneAmp;
    snNoiseAmp *= snNoiseDec; snToneAmp *= snToneDec;
  }
  if (hatAmp > 0.001f) { out += hp * hatAmp; hatAmp *= hatDec; }
  if (crAmp  > 0.001f) { out += hp * crAmp;  crAmp  *= crDec;  }

  if (bassAmp > 0.001f) {                       // bass: filtered square wave
    bassPhase += bassFreq / SR; if (bassPhase >= 1) bassPhase -= 1;
    float sq = bassPhase < 0.5f ? 1.0f : -1.0f;
    bassLP += 0.2f * (sq - bassLP);
    out += bassLP * bassAmp;
    bassAmp *= bassDec;
  }

  padPh1 += padF1 / SR; if (padPh1 >= 1) padPh1 -= 1;   // pad: two saws, filtered
  padPh2 += padF2 / SR; if (padPh2 >= 1) padPh2 -= 1;
  padFilt.process((padPh1 * 2 - 1) + (padPh2 * 2 - 1) * 0.7f);
  out += padFilt.low * padGain;

  if (riserGain > 0) { riserFilt.process(n); out += riserFilt.band * riserGain; }

  out *= MASTER_VOL * 0.5f;
  return out / (1.0f + fabsf(out));             // soft clipping, never harsh overflow
}

// ---------------- Audio output ----------------
dac_continuous_handle_t dacHandle;
uint8_t audioBuf[BLOCK];

void audioTask(void*) {
  for (;;) {
    updateControls();
    for (int i = 0; i < BLOCK; i++) {
      int v = 128 + (int)(renderSample() * 127.0f);
      audioBuf[i] = (uint8_t)constrain(v, 0, 255);
    }
    size_t loaded;
    dac_continuous_write(dacHandle, audioBuf, BLOCK, &loaded, -1);   // waits until there's room
  }
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
  else if (s == "E REV")   evRev = true;
  else if (s == "E TOSS")  evToss = true;
  else if (s == "E CATCH") evCatch = true;
}

bool handleShortcut(char c) {
  switch (c) {
    case '0': rawSpeed = 0;   return true;
    case '1': rawSpeed = 250; return true;
    case '2': rawSpeed = 550; return true;
    case '3': rawSpeed = 850; return true;
    case 'v': rawPlane = 0;   return true;
    case 'h': rawPlane = 1;   return true;
    case 't': evToss = true;  return true;
    case 'c': evCatch = true; return true;
    case 'r': evRev = true;   return true;
  }
  return false;
}

String lineBuf;

// ---------------- Setup & loop ----------------
void setup() {
  Serial.begin(115200);

  kickDec    = decayCoef(0.18f);
  snNoiseDec = decayCoef(0.08f);
  snToneDec  = decayCoef(0.05f);
  hatDec     = decayCoef(0.03f);
  crDec      = decayCoef(0.8f);
  bassDec    = decayCoef(0.2f);
  padFilt.q = 1.0f;
  riserFilt.q = 0.3f;
  samplesPerStep = SR * 60.0f / BPM / 4.0f;

  dac_continuous_config_t cfg = {};
  cfg.chan_mask = DAC_CHANNEL_MASK_CH0;          // CH0 = GPIO 25
  cfg.desc_num  = 4;
  cfg.buf_size  = 256;                           // small buffers = low delay
  cfg.freq_hz   = SR;
  cfg.offset    = 0;
  cfg.clk_src   = DAC_DIGI_CLK_SRC_DEFAULT;
  cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
  ESP_ERROR_CHECK(dac_continuous_new_channels(&cfg, &dacHandle));
  ESP_ERROR_CHECK(dac_continuous_enable(dacHandle));

  xTaskCreatePinnedToCore(audioTask, "audio", 4096, NULL, 5, NULL, 1);

  Serial.println("SpinBeats ESP32 ready.");
  Serial.println("Speed: 0 1 2 3   Plane: v h   Toss/catch: t c   Reversal: r");
}

unsigned long lastPrint = 0;

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (lineBuf.length() == 0 && handleShortcut(c)) continue;   // single-key commands
    if (c == '\n' || c == '\r') {
      if (lineBuf.length()) handleCommand(lineBuf);
      lineBuf = "";
    } else {
      lineBuf += c;
    }
  }

  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    Serial.printf("speed %d  level %d  section %c %s\n",
                  (int)rawSpeed, dispLevel, dispSection ? 'B' : 'A',
                  dispToss ? " [TOSS]" : "");
  }
  delay(2);
}
