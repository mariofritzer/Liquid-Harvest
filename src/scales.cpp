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
// adaptive Glättung + Diagnose
static float    noiseVar[NUM_PLACES];
static float    trendSm[NUM_PLACES];
static uint32_t fastUntil[NUM_PLACES];          // Rauschen (Varianz in g²) um den geglätteten Wert
static float    ring[NUM_PLACES][16];          // letzte schnelle Werte (Mittelwert für Endmessungen)
static uint8_t  ringN[NUM_PLACES], ringPos[NUM_PLACES];
static uint32_t lastSample = 0;
static float    rateHz = 0;

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

static void process(int i, int32_t raw, uint32_t now, float dt) {
  hist[i][histPos] = raw;
  if (histN[i] < 3) histN[i]++;
  int32_t med = (histN[i] < 3) ? raw : median3(hist[i][0], hist[i][1], hist[i][2]);   // Ausreißer weg
  float f = cfg.factor[i];
  if (histN[i] == 1) { rawSm[i] = trendSm[i] = med; noiseVar[i] = 0.04f; fastUntil[i] = 0; }
  else {
    // Adaptiver Filter: in Ruhe stark glätten (τ 2 s), bei echten Änderungen (Glas drauf/weg,
    // Einschenken) für kurze Zeit schnell folgen. "Echte Änderung" = Sprung über 4σ des gemessenen
    // Rauschens oder ein schneller Hilfsfilter (τ 0,3 s) läuft davon (Trend).
    float dg = (med - rawSm[i]) / f;
    float sigma = sqrtf(noiseVar[i]);
    trendSm[i] += (med - trendSm[i]) * (1.0f - expf(-dt / 0.3f));
    bool jump  = fabsf(dg) > max(0.6f, 4.0f * sigma);
    bool trend = fabsf(trendSm[i] - rawSm[i]) / f > max(0.4f, 1.5f * sigma);
    if (jump || trend) fastUntil[i] = now + 600;
    float tau = (int32_t)(now - fastUntil[i]) < 0 ? 0.1f : (int32_t)(now - fastUntil[i]) < 1200 ? 0.4f : 2.0f;
    rawSm[i] += (med - rawSm[i]) * (1.0f - expf(-dt / tau));
    if ((int32_t)(now - fastUntil[i]) >= 0) noiseVar[i] += (dg * dg - noiseVar[i]) * 0.03f;   // Rauschen nur in Ruhe schätzen
  }
  gFast[i]   = (med - cfg.offset[i]) / f;
  gSmooth[i] = (rawSm[i] - cfg.offset[i]) / f;

  ring[i][ringPos[i]] = gFast[i];
  ringPos[i] = (ringPos[i] + 1) % 16;
  if (ringN[i] < 16) ringN[i]++;

  // "ruhig": geglätteter Wert bleibt in einem Band, das sich nach dem gemessenen Rauschen richtet
  float band = max(0.15f, 1.5f * sqrtf(noiseVar[i])) + 0.002f * fabsf(gSmooth[i]);
  if (fabsf(gSmooth[i] - stabRef[i]) > band) { stabRef[i] = gSmooth[i]; stabSince[i] = now; }
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
    float dt = lastSample ? constrain((now - lastSample) / 1000.0f, 0.005f, 0.5f) : 0.1f;
    if (lastSample) rateHz += (1.0f / dt - rateHz) * 0.05f;
    lastSample = now;
    for (int i = 0; i < NUM_PLACES; i++) {
      if (!(wait & (1 << i))) continue;
      process(i, signExtend24(v[i]), now, dt);
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

// ---- Öffentliche Funktionen: Index = Glas-Platz, intern wird auf die Waage (HX711) umgesetzt
static inline int C(int p) { return cfg.scaleMap[constrain(p, 0, NUM_PLACES - 1)]; }
uint32_t seq()            { return seqNo; }
bool     ok(int p)        { if (p < 0 || p >= NUM_PLACES || !(cfg.posMask & (1 << p))) return false; int i = C(p); return healthy[i] && histN[i] >= 3; }
float    g(int p)         { return gFast[C(p)]; }
float    gs(int p)        { return gSmooth[C(p)]; }
int32_t  raw(int p)       { return (int32_t)rawSm[C(p)]; }
bool     stable(int p)    { int i = C(p); return ok(p) && millis() - stabSince[i] >= cfg.stableMs; }
float    noise(int p)     { return sqrtf(noiseVar[C(p)]); }
float    hz()             { return rateHz; }
float    chG(int ch)      { return (ch >= 0 && ch < NUM_PLACES && healthy[ch]) ? gFast[ch] : NAN; }
float    avg(int p, int n) {
  int i = C(p);
  n = constrain(n, 1, (int)ringN[i]);
  if (!n) return gSmooth[i];
  float s = 0;
  for (int k = 0; k < n; k++) s += ring[i][(ringPos[i] + 16 - 1 - k) % 16];
  return s / n;
}

// Kalibrierung bleibt bei der Waage (Kanal), nicht beim Glas – Umordnen verliert nichts
void tare(int p) {
  int i = C(p);
  cfg.offset[i] = (int32_t)rawSm[i];
  for (int k = 0; k < 16; k++) ring[i][k] = 0;
  gFast[i] = gSmooth[i] = 0;
  stabRef[i] = 0;
  stabSince[i] = millis();
}

bool calibrate(int p, float known) {
  int i = C(p);
  if (known <= 0) return false;
  float delta = rawSm[i] - cfg.offset[i];
  if (fabsf(delta) < 500) return false;   // zu wenig Signal -> Gewicht vergessen?
  cfg.factor[i] = delta / known;           // Vorzeichen bleibt -> verkehrt montierte Zelle ok
  return true;
}

void nudgeZero(int p, float frac) {
  int i = C(p);
  cfg.offset[i] += (int32_t)((rawSm[i] - cfg.offset[i]) * frac);
}

}  // namespace Scales
