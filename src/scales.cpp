#include "scales.h"
#include "settings.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_struct.h"

namespace Scales {

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t bitIn1[NUM_PLACES];          // Bitmaske im GPIO_IN1_REG
static int32_t  hist[NUM_PLACES][3];
static uint8_t  histN[NUM_PLACES];
static uint8_t  histPos = 0;
static float    rawSm[NUM_PLACES];
static float    gFast[NUM_PLACES], gSmooth[NUM_PLACES];
static float    stabRef[NUM_PLACES];
static uint32_t stabSince[NUM_PLACES];
static bool     healthy[NUM_PLACES];
static uint32_t lastReady[NUM_PLACES];
static uint32_t nextRetry[NUM_PLACES];
static uint32_t seqNo = 0;

static constexpr uint32_t SCK_BIT = 1UL << PIN_HX_SCK;

// Liest 24 Bit von allen 6 HX711 gleichzeitig + 1 Takt für Kanal A / Gain 128.
// SCK darf max. ~60 µs HIGH bleiben, deshalb in einer kritischen Sektion.
static void readAll(uint32_t out[NUM_PLACES]) {
  uint32_t v[NUM_PLACES] = {0};
  portENTER_CRITICAL(&mux);
  for (int b = 0; b < 24; b++) {
    GPIO.out_w1ts = SCK_BIT;
    delayMicroseconds(1);
    uint32_t r = REG_READ(GPIO_IN1_REG);
    GPIO.out_w1tc = SCK_BIT;
    for (int i = 0; i < NUM_PLACES; i++) v[i] = (v[i] << 1) | ((r & bitIn1[i]) ? 1u : 0u);
    delayMicroseconds(1);
  }
  GPIO.out_w1ts = SCK_BIT;   // 25. Puls -> nächste Messung Kanal A, Gain 128
  delayMicroseconds(1);
  GPIO.out_w1tc = SCK_BIT;
  portEXIT_CRITICAL(&mux);
  for (int i = 0; i < NUM_PLACES; i++) out[i] = v[i];
}

static inline int32_t signExtend24(uint32_t v) { return (int32_t)(v << 8) >> 8; }

static int32_t median3(int32_t a, int32_t b, int32_t c) {
  if (a > b) std::swap(a, b);
  if (b > c) std::swap(b, c);
  if (a > b) std::swap(a, b);
  return b;
}

void begin() {
  pinMode(PIN_HX_SCK, OUTPUT);
  digitalWrite(PIN_HX_SCK, LOW);
  uint32_t now = millis();
  for (int i = 0; i < NUM_PLACES; i++) {
    pinMode(PIN_HX_DT[i], INPUT);
    bitIn1[i] = 1UL << (PIN_HX_DT[i] - 32);
    healthy[i] = true;
    lastReady[i] = now;
    histN[i] = 0;
    stabSince[i] = now;
  }
}

static void process(int i, int32_t raw, uint32_t now) {
  hist[i][histPos] = raw;
  if (histN[i] < 3) histN[i]++;
  int32_t med = (histN[i] < 3) ? raw : median3(hist[i][0], hist[i][1], hist[i][2]);
  if (histN[i] == 1) rawSm[i] = med;
  else rawSm[i] = rawSm[i] * 0.7f + med * 0.3f;

  float f = cfg.factor[i];
  gFast[i]   = (med - cfg.offset[i]) / f;
  gSmooth[i] = (rawSm[i] - cfg.offset[i]) / f;

  float band = 0.3f + 0.002f * fabsf(gFast[i]);
  if (fabsf(gFast[i] - stabRef[i]) > band) { stabRef[i] = gFast[i]; stabSince[i] = now; }
}

void loop() {
  uint32_t now = millis();
  uint32_t in = REG_READ(GPIO_IN1_REG);
  uint8_t wait = 0, ready = 0;

  for (int i = 0; i < NUM_PLACES; i++) {
    if (!(cfg.enabledMask & (1 << i))) { healthy[i] = false; continue; }
    bool rdy = !(in & bitIn1[i]);
    if (rdy) lastReady[i] = now;
    if (!healthy[i]) {
      if (rdy && now >= nextRetry[i]) { healthy[i] = true; histN[i] = 0; }
      else continue;
    }
    wait |= 1 << i;
    if (rdy) ready |= 1 << i;
  }
  if (!wait) return;

  if (ready == wait) {
    uint32_t v[NUM_PLACES];
    readAll(v);
    for (int i = 0; i < NUM_PLACES; i++) {
      if (!(wait & (1 << i))) continue;
      process(i, signExtend24(v[i]), now);
    }
    histPos = (histPos + 1) % 3;
    seqNo++;
  } else {
    // Waage, die nicht antwortet, nach 600 ms aus dem Verbund nehmen
    for (int i = 0; i < NUM_PLACES; i++) {
      if ((wait & (1 << i)) && !(ready & (1 << i)) && now - lastReady[i] > 600) {
        healthy[i] = false;
        nextRetry[i] = now + 5000;
        Serial.printf("[Waage %d] antwortet nicht\n", i + 1);
      }
    }
  }
}

uint32_t seq()            { return seqNo; }
bool     ok(int i)        { return healthy[i] && histN[i] >= 3; }
float    g(int i)         { return gFast[i]; }
float    gs(int i)        { return gSmooth[i]; }
int32_t  raw(int i)       { return (int32_t)rawSm[i]; }
bool     stable(int i)    { return ok(i) && millis() - stabSince[i] >= cfg.stableMs; }

void tare(int i) {
  cfg.offset[i] = (int32_t)rawSm[i];
  gFast[i] = gSmooth[i] = 0;
  stabRef[i] = 0;
  stabSince[i] = millis();
}

bool calibrate(int i, float known) {
  if (known <= 0) return false;
  float delta = rawSm[i] - cfg.offset[i];
  if (fabsf(delta) < 500) return false;   // zu wenig Signal -> Gewicht vergessen?
  cfg.factor[i] = delta / known;           // Vorzeichen bleibt -> verkehrt montierte Zelle ok
  return true;
}

void nudgeZero(int i, float frac) {
  cfg.offset[i] += (int32_t)((rawSm[i] - cfg.offset[i]) * frac);
}

}  // namespace Scales
