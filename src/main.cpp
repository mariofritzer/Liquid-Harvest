// =====================================================================
//  Liquid Harvest – automatischer Schnaps-Mähdrescher (6 Gläser)
//  ESP32 · 6x HX711 · MG90S · Peristaltikpumpe · WS2812B · KY-040
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <vector>

#include "pins.h"
#include "version.h"
#include "settings.h"
#include "scales.h"
#include "improv_serial.h"
#include "web_index.h"

// ---------------------------------------------------------------------
//  Zustände
// ---------------------------------------------------------------------
enum PlaceState : uint8_t { P_NONE = 0, P_GLASS = 1, P_FILLED = 2 };

struct Place {
  PlaceState st = P_NONE;
  float    tare = 0;          // Gewicht beim Abstellen (Leerglas)
  float    lastMl = 0;        // zuletzt eingeschenkt
  uint32_t errUntil = 0;      // rote Fehleranzeige bis
  uint32_t belowSince = 0;
  bool     zeroDone = false;  // Einschalt-Nullung erledigt
};
Place places[NUM_PLACES];

enum Phase : uint8_t { PH_IDLE, PH_MOVE, PH_SETTLE, PH_FAST, PH_SLOW, PH_DRIP, PH_PRIME };
static const char* PHASE_NAMES[] = {"idle", "move", "settle", "fast", "slow", "drip", "prime"};

enum Source : uint8_t { SRC_BUTTON, SRC_WEB, SRC_AUTO };

struct Job { int8_t p; float ml; Source src; };
static std::vector<Job> jobs;

struct Pour {
  Phase    ph = PH_IDLE;
  int8_t   p = -1;
  float    ml = 0, target = 0, startW = 0, got = 0, progRef = 0;
  uint32_t t0 = 0, tPhase = 0, lastProg = 0;
  Source   src = SRC_WEB;
  // Entlüften
  uint32_t primeUntil = 0;
  bool     primeRunning = false, primeByButton = false;
} pour;

static int   aimIdx = 0;            // 0 = Park, 1..6 = Glas
static float servoCur = 0, servoTgt = 0;
static uint8_t pumpPct = 0;
static uint32_t btnErrUntil = 0;
static uint32_t btnAckUntil = 0;   // Taster-LED kurz aus = Klick angenommen
static volatile bool otaRunning = false;
static uint32_t rebootAt = 0;
static bool fsOk = false;
static volatile bool imgChanged = false;     // Upload/Löschen fertig -> Hauptschleife meldet es
static volatile bool vidChanged = false;
static File imgFile;
static bool imgTooBig = false;
static constexpr size_t IMG_MAX = 100 * 1024;

Adafruit_NeoPixel strip(MAX_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800);
static uint8_t ledTest[MAX_LEDS_PER_GLASS * 2];   // LED-Nummern, die gerade zum Testen leuchten
static uint8_t ledTestN = 0;
static uint32_t ledTestUntil = 0;

// Kettenlänge übernehmen (alte, nun überzählige LEDs vorher ausschalten)
static void applyLedCount() {
  if (strip.numPixels() == cfg.ledCount) return;
  strip.clear(); strip.show();
  strip.updateLength(cfg.ledCount);
  strip.clear(); strip.show();
}

// Web
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;
static SemaphoreHandle_t cmdMutex;
static std::vector<String> cmdQueue;   // Befehle aus Web/REST -> Hauptschleife
static String stateCache = "{}";
static SemaphoreHandle_t stateMutex;

// WLAN
enum WState : uint8_t { W_CONNECTING, W_STA, W_AP };
static WState wstate = W_AP;
static bool apActive = false;
static bool apOpen = false;        // Hotspot ohne Passwort (per Encoder-Knopf erzwungen)
static bool apUserOff = false;     // Hotspot per Knopf ausgeschaltet -> kein automatischer Rückfall-Hotspot
static uint32_t ledFlashUntil = 0, ledFlashColor = 0;
static uint32_t tWifi = 0, tStaRetry = 0;
static bool staRetrying = false;
static String apSsid;
static bool scanPending = false;

// ---------------------------------------------------------------------
//  Hilfen: Events an Web + Serial
// ---------------------------------------------------------------------
static void wsSend(JsonDocument& d) {
  if (!ws.count()) return;
  String s;
  serializeJson(d, s);
  ws.textAll(s);
}

static void event(const char* e, int p, float ml = -1, const char* reason = nullptr) {
  Serial.printf("[Event] %s Glas %d", e, p + 1);
  if (ml >= 0) Serial.printf(" %.1f ml", ml);
  if (reason) Serial.printf(" (%s)", reason);
  Serial.println();
  JsonDocument d;
  d["t"] = "ev"; d["e"] = e; d["p"] = p + 1;
  if (ml >= 0) d["ml"] = roundf(ml * 10) / 10;
  if (reason) d["r"] = reason;
  wsSend(d);
}

static void toast(const String& msg) {
  JsonDocument d;
  d["t"] = "toast"; d["m"] = msg;
  wsSend(d);
}

static String setupInfoStr();
static void sendCfg() {
  JsonDocument d;
  d["t"] = "cfg";
  settingsToJson(d["cfg"].to<JsonObject>());
  d["ver"] = FW_VERSION;
  d["setup"] = setupInfoStr();
  wsSend(d);
}

// ---------------------------------------------------------------------
//  Aktoren
// ---------------------------------------------------------------------
static bool servoPowered = true;
static void servoWrite(float deg) {
  float us = cfg.servoMinUs + (cfg.servoMaxUs - cfg.servoMinUs) * (deg / 180.0f);
  ledcWrite(CH_SERVO, (uint32_t)(us * 65535.0f / 20000.0f));
  servoPowered = true;
}

static float angleFor(int idx) { return idx <= 0 ? cfg.angPark : cfg.ang[idx - 1]; }

static void aim(int idx) {
  aimIdx = constrain(idx, 0, NUM_PLACES);
  servoTgt = angleFor(aimIdx);
}

static bool servoArrived() { return fabsf(servoCur - servoTgt) < 0.5f; }

static void servoLoop() {
  static uint32_t last = micros();
  static float written = -1;
  uint32_t now = micros();
  float dt = (now - last) / 1e6f;
  last = now;
  float step = cfg.servoSpeed * dt;
  float diff = servoTgt - servoCur;
  if (fabsf(diff) <= step) servoCur = servoTgt;
  else servoCur += diff > 0 ? step : -step;
  if (fabsf(servoCur - written) >= 0.1f || (!servoPowered && servoCur != servoTgt)) { servoWrite(servoCur); written = servoCur; }

  // An der Zielposition: kurz halten, dann Impulse abschalten -> Servo stromlos, kein Brummen/Zittern
  static uint32_t arrivedAt = 0;
  static float lastTgt = -1;
  if (servoTgt != lastTgt) { lastTgt = servoTgt; arrivedAt = 0; if (!servoPowered) { servoWrite(servoCur); written = servoCur; } }
  if (servoCur == servoTgt) {
    if (!arrivedAt) arrivedAt = millis();
    if (cfg.servoRelease && servoPowered && millis() - arrivedAt > cfg.servoHoldMs) {
      ledcWrite(CH_SERVO, 0);
      servoPowered = false;
    }
  } else arrivedAt = 0;
}

static void pumpSet(uint8_t pct) {
  pumpPct = pct;
  ledcWrite(CH_PUMP, (uint32_t)pct * 255 / 100);
}

// ---------------------------------------------------------------------
//  Ausschank-Logik
// ---------------------------------------------------------------------
static bool pourBusy() { return pour.ph != PH_IDLE || !jobs.empty(); }
static bool pourActiveOn(int i) { return pour.ph >= PH_MOVE && pour.ph <= PH_DRIP && pour.p == i; }

static bool enqueue(int p, float ml, Source src) {
  if (p < 0 || p >= NUM_PLACES) return false;
  if (places[p].st == P_NONE) { toast("Kein Glas auf Platz " + String(p + 1)); return false; }
  if (pour.ph == PH_PRIME) return false;
  if (pourActiveOn(p)) return false;
  for (auto& j : jobs) if (j.p == p) return false;
  jobs.push_back({(int8_t)p, constrain(ml, 1.0f, 100.0f), src});
  return true;
}

static int autofill(float ml) {
  // Leere (blaue) Gläser in Fahrtrichtung sortiert einreihen
  std::vector<int> idx;
  for (int i = 0; i < NUM_PLACES; i++) if (places[i].st == P_GLASS) idx.push_back(i);
  bool asc = servoCur <= 90;
  std::sort(idx.begin(), idx.end(), [asc](int a, int b) {
    return asc ? cfg.ang[a] < cfg.ang[b] : cfg.ang[a] > cfg.ang[b];
  });
  int n = 0;
  for (int i : idx) if (enqueue(i, ml, SRC_AUTO)) n++;
  if (!n) { toast("Keine leeren Gläser gefunden"); btnErrUntil = millis() + 1200; }
  return n;
}

static void finishJob() {
  Source src = pour.src;
  pour.ph = PH_IDLE;
  pour.p = -1;
  if (jobs.empty()) {
    if (src == SRC_AUTO || (src == SRC_WEB && cfg.parkAfter)) aim(0);
  }
}

static void abortPour(const char* reason, bool clearQueue) {
  pumpSet(0);
  if (pour.p >= 0) {
    places[pour.p].errUntil = millis() + 3000;
    event("pour_error", pour.p, pour.got / cfg.density, reason);
  }
  btnErrUntil = millis() + 1500;
  if (clearQueue) jobs.clear();
  finishJob();
}

static void stopAll(const char* reason) {
  bool was = pourBusy();
  jobs.clear();
  if (pour.ph == PH_PRIME) { pumpSet(0); pour.ph = PH_IDLE; }
  else if (pour.ph != PH_IDLE) abortPour(reason, true);
  pumpSet(0);
  if (was) toast("Gestoppt");
}

static void startPrime(uint32_t ms, bool byButton) {
  if (pourBusy()) return;
  aim(0);
  pour.ph = PH_PRIME;
  pour.primeRunning = false;
  pour.primeByButton = byButton;
  pour.primeUntil = ms;   // Dauer, Start erst in Parkposition
}

static void pourLoop(bool btnHeld) {
  uint32_t now = millis();

  // Sicherheitsnetz: Pumpe nie während einer Servo-Fahrt
  if (pumpPct && !servoArrived()) pumpSet(0);

  switch (pour.ph) {
    case PH_IDLE:
      if (!jobs.empty()) {
        Job j = jobs.front();
        jobs.erase(jobs.begin());
        if (places[j.p].st == P_NONE) break;   // Glas inzwischen weg
        pour.p = j.p; pour.ml = j.ml; pour.src = j.src;
        pour.target = j.ml * cfg.density;
        pour.got = 0;
        pour.ph = PH_MOVE;
        aim(j.p + 1);
      }
      break;

    case PH_MOVE:
      if (places[pour.p].st == P_NONE) { abortPour("glass_removed", false); break; }
      if (servoArrived()) { pour.ph = PH_SETTLE; pour.tPhase = now; }
      break;

    case PH_SETTLE:
      if (places[pour.p].st == P_NONE) { abortPour("glass_removed", false); break; }
      if (now - pour.tPhase >= cfg.settleMs) {
        pour.startW = Scales::gs(pour.p);
        pour.progRef = 0;
        pour.t0 = pour.lastProg = now;
        pour.ph = PH_FAST;
        pumpSet(pour.target - cfg.slowBefore <= 0 ? cfg.pwmSlow : cfg.pwmFast);
        if (pour.target - cfg.slowBefore <= 0) pour.ph = PH_SLOW;
      }
      break;

    case PH_FAST:
    case PH_SLOW: {
      int p = pour.p;
      if (!Scales::ok(p)) { abortPour("scale_error", true); break; }
      float w = Scales::g(p);
      pour.got = w - pour.startW;
      if (w < pour.startW - 2.0f || w < cfg.glassMin * 0.6f) { places[p].st = P_NONE; event("glass_off", p); abortPour("glass_removed", false); break; }
      if (pour.got > pour.progRef + 0.4f) { pour.progRef = pour.got; pour.lastProg = now; }
      if (now - pour.lastProg > cfg.noFlowMs) { abortPour("no_flow", true); break; }
      if (now - pour.t0 > cfg.maxPourMs) { abortPour("timeout", true); break; }
      if (pour.got >= pour.target - cfg.stopBefore) {
        pumpSet(0);
        pour.ph = PH_DRIP;
        pour.tPhase = now;
      } else if (pour.ph == PH_FAST && pour.got >= pour.target - cfg.slowBefore) {
        pumpSet(cfg.pwmSlow);
        pour.ph = PH_SLOW;
      }
      break;
    }

    case PH_DRIP:
      if (now - pour.tPhase >= 800) {
        int p = pour.p;
        pour.got = Scales::gs(p) - pour.startW;
        places[p].st = P_FILLED;
        places[p].lastMl = pour.got / cfg.density;
        event("pour_done", p, places[p].lastMl);
        finishJob();
      }
      break;

    case PH_PRIME:
      if (!pour.primeRunning) {
        if (servoArrived()) {
          pour.primeRunning = true;
          pour.primeUntil = now + pour.primeUntil;
          pumpSet(cfg.pwmFast);
        }
      } else if (now >= pour.primeUntil || (pour.primeByButton && !btnHeld)) {
        pumpSet(0);
        pour.ph = PH_IDLE;
      }
      break;
  }
}

// ---------------------------------------------------------------------
//  Glaserkennung
// ---------------------------------------------------------------------
static void placesLoop() {
  static uint32_t lastSeq = 0;
  if (Scales::seq() == lastSeq) return;
  lastSeq = Scales::seq();
  uint32_t now = millis();

  for (int i = 0; i < NUM_PLACES; i++) {
    Place& pl = places[i];
    if (!Scales::ok(i)) {
      if (pl.st != P_NONE && !pourActiveOn(i)) { pl.st = P_NONE; event("glass_off", i); }
      continue;
    }
    float g = Scales::g(i), gs = Scales::gs(i);
    bool stb = Scales::stable(i);

    // Einschalt-Nullung: leere Waage nach dem Start automatisch tarieren
    if (!pl.zeroDone) {
      if ((stb && now > 2500) || now > 8000) {
        if (fabsf(gs) < 10.0f) Scales::tare(i);
        pl.zeroDone = true;
      }
      continue;
    }

    switch (pl.st) {
      case P_NONE:
        if (g > cfg.glassMin && stb) {
          pl.st = P_GLASS;
          pl.tare = gs;
          pl.lastMl = 0;
          event("glass_on", i);                 // == "Glas_X_da"
        } else if (stb && fabsf(gs) < cfg.glassMin * 0.5f) {
          Scales::nudgeZero(i, 0.05f);          // langsame Drift-Kompensation
        } else if (stb && gs < -cfg.glassMin * 0.6f) {
          Scales::tare(i);                      // Glas stand beim Nullen drauf
        }
        break;

      case P_GLASS:
      case P_FILLED:
        if (pourActiveOn(i)) { pl.belowSince = 0; break; }
        if (g < cfg.glassMin * 0.6f) {
          if (!pl.belowSince) pl.belowSince = now;
          if (now - pl.belowSince > 250) {
            pl.st = P_NONE;
            pl.belowSince = 0;
            event("glass_off", i);
          }
        } else pl.belowSince = 0;
        break;
    }
  }
}

// ---------------------------------------------------------------------
//  Bedienung: Taster + Drehencoder
// ---------------------------------------------------------------------
static volatile int32_t encCount = 0;
static volatile uint8_t encState = 0;
static const int8_t QTAB[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void IRAM_ATTR encIsr() {
  uint8_t s = (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT);
  encState = ((encState << 2) | s) & 0x0F;
  encCount += QTAB[encState];
}

struct Button {
  int pin;
  uint32_t longMs;
  Button(int p, uint32_t l) : pin(p), longMs(l) {}
  bool stable = false, last = false;
  uint32_t tChange = 0, tDown = 0, tUp = 0;
  uint8_t clicks = 0;
  bool longFired = false, consumed = false;
  // Ergebnis pro Aufruf
  bool pressedEdge = false;
  uint8_t clickResult = 0;
  bool longResult = false;

  void update() {
    pressedEdge = false; clickResult = 0; longResult = false;
    uint32_t now = millis();
    bool raw = digitalRead(pin) == LOW;
    if (raw != last) { last = raw; tChange = now; }
    if (now - tChange > 25 && raw != stable) {
      stable = raw;
      if (stable) { tDown = now; longFired = false; consumed = false; pressedEdge = true; }
      else if (!longFired && !consumed) { clicks++; tUp = now; }
    }
    if (stable && !longFired && !consumed && now - tDown >= longMs) { longFired = true; longResult = true; clicks = 0; }
    if (!stable && clicks && now - tUp > 350) { clickResult = clicks; clicks = 0; }
  }
};

Button btn{PIN_BTN, 1500};
Button encBtn{PIN_ENC_SW, 3000};

static void toggleForcedAP();

static void inputLoop() {
  btn.update();
  encBtn.update();

  // Notstopp: jeder Tastendruck während des Ausschanks
  if (btn.pressedEdge && pourBusy() && pour.ph != PH_PRIME) {
    stopAll("button_stop");
    btn.consumed = true;
    return;
  }

  if (btn.clickResult && !pourBusy()) {
    float ml = btn.clickResult >= 2 ? cfg.doubleMl : cfg.shotMl;
    bool ok = aimIdx == 0 ? autofill(ml) > 0 : enqueue(aimIdx - 1, ml, SRC_BUTTON);
    if (ok) btnAckUntil = millis() + 140;
    else btnErrUntil = millis() + 1200;
  }
  if (btn.longResult && aimIdx == 0 && !pourBusy()) startPrime(15000, true);  // halten = entlüften

  if (encBtn.clickResult && !pourBusy()) aim(0);
  if (encBtn.longResult) toggleForcedAP();   // 3 s: offenen Hotspot ein / aus

  // Drehencoder -> Rohr live schwenken
  static int32_t lastCount = 0, acc = 0;
  int32_t c = encCount;
  acc += c - lastCount;
  lastCount = c;
  while (abs(acc) >= cfg.encDiv) {
    int dir = acc > 0 ? 1 : -1;
    acc -= dir * cfg.encDiv;
    if (cfg.encReverse) dir = -dir;
    if (!pourBusy()) aim(aimIdx + dir);
  }
}

// ---------------------------------------------------------------------
//  LEDs
// ---------------------------------------------------------------------
static uint32_t scaleColor(float r, float g, float b, float k) {
  k *= cfg.ledBright / 100.0f;
  return strip.Color((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k));
}

// Farbe für einen LED-Zustand mit Effekt (0 an, 1 pulsieren, 2 schnell pulsieren, 3 blinken)
static uint32_t fxColor(uint32_t col, uint8_t lvl, uint8_t fx, uint32_t now) {
  float k = lvl / 100.0f * cfg.ledBright / 100.0f;
  if (fx == 1) k *= 0.3f + 0.7f * (0.5f + 0.5f * sinf(now * 2 * PI / 1400.0f));
  else if (fx == 2) k *= 0.3f + 0.7f * (0.5f + 0.5f * sinf(now * 2 * PI / 450.0f));
  else if (fx == 3 && (now / 150) % 2) k = 0;
  uint32_t r = (col >> 16) & 255, g = (col >> 8) & 255, b = col & 255;
  if (lvl && k > 0 && fx != 3) {   // sehr schwache Einstellungen nicht ganz verschwinden lassen
    r = r ? max<uint32_t>(1, r * k) : 0; g = g ? max<uint32_t>(1, g * k) : 0; b = b ? max<uint32_t>(1, b * k) : 0;
    return strip.Color(r, g, b);
  }
  return strip.Color(r * k, g * k, b * k);
}
static uint32_t stateColor(int s, uint32_t now) { return fxColor(cfg.ledCol[s], cfg.ledLvl[s], cfg.ledFx[s], now); }
static uint32_t ledPrevUntil = 0, ledPrevCol = 0;
static uint8_t ledPrevLvl = 100, ledPrevFx = 0;

static void ledsLoop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 20) return;
  last = now;

  bool blink = (now / 150) % 2;

  uint32_t placeCol[NUM_PLACES];
  for (int i = 0; i < NUM_PLACES; i++) {
    const Place& pl = places[i];
    uint32_t c;
    if (!(cfg.enabledMask & (1 << i)))       c = 0;
    else if (now < ledPrevUntil)              c = fxColor(ledPrevCol, ledPrevLvl, ledPrevFx, now);   // Farbvorschau
    else if (now < pl.errUntil)               c = stateColor(5, now);   // Fehler
    else if (!Scales::ok(i))                  c = stateColor(6, now);   // Waage defekt
    else if (pourActiveOn(i))                 c = stateColor(3, now);   // schenkt ein
    else if (aimIdx == i + 1 && !pourBusy())  c = stateColor(2, now);   // Rohr zielt
    else if (pl.st == P_GLASS)                c = stateColor(1, now);   // bereit
    else if (pl.st == P_FILLED)               c = stateColor(4, now);   // fertig
    else                                       c = stateColor(0, now);   // kein Glas
    placeCol[i] = c;
  }
  // Jede LED bekommt die Farbe ihres Glases; nicht zugeordnete LEDs bleiben aus
  strip.clear();
  for (int i = 0; i < NUM_PLACES; i++)
    for (int k = 0; k < cfg.ledMapN[i]; k++)
      if (cfg.ledMap[i][k] >= 1 && cfg.ledMap[i][k] <= strip.numPixels()) strip.setPixelColor(cfg.ledMap[i][k] - 1, placeCol[i]);
  if (now < ledTestUntil) {    // Test aus den Einstellungen: gewählte LEDs blinken weiß
    uint32_t w = blink ? strip.Color(255, 255, 255) : strip.Color(40, 40, 40);
    for (int k = 0; k < ledTestN; k++) if (ledTest[k] >= 1 && ledTest[k] <= strip.numPixels()) strip.setPixelColor(ledTest[k] - 1, w);
  }
  if (now < ledFlashUntil) {   // kurze Rückmeldung: blau = Hotspot an, rot = aus
    float k = (cfg.ledBright / 100.0f) * (0.4f + 0.6f * ((now / 150) % 2));
    uint8_t r = (ledFlashColor >> 16) & 255, g = (ledFlashColor >> 8) & 255, b = ledFlashColor & 255;
    for (int i = 0; i < (int)strip.numPixels(); i++) strip.setPixelColor(i, strip.Color(r * k, g * k, b * k));
  }
  strip.show();

  // Taster-Beleuchtung: Zustand bestimmen, Effekt aus den Einstellungen
  int bs;
  if (now < btnErrUntil) bs = 5;                                   // Fehler
  else if (pourBusy()) bs = 4;                                     // schenkt ein (Drücken = Stopp)
  else if (aimIdx > 0) bs = places[aimIdx - 1].st != P_NONE ? 2 : 3; // Rohr über Glas / leerem Platz
  else {
    int empty = 0, full = 0;
    for (int i = 0; i < NUM_PLACES; i++) { if (places[i].st == P_GLASS) empty++; else if (places[i].st == P_FILLED) full++; }
    bs = empty ? 0 : (full ? 6 : 1);                               // bereit / alle gefüllt / keine Gläser
  }
  float d;
  switch (cfg.btnFx[bs]) {
    case 0:  d = 0; break;                                                         // aus
    case 2:  d = 0.18f; break;                                                     // gedimmt
    case 3:  d = 0.08f + 0.92f * (0.5f + 0.5f * sinf(now * 2 * PI / 3000.0f)); break;  // atmen
    case 4:  d = 0.12f + 0.88f * (0.5f + 0.5f * sinf(now * 2 * PI / 500.0f)); break;   // pulsieren
    case 5:  d = ((now / 120) % 2) ? 1 : 0; break;                                 // blinken
    case 6: { uint32_t t = now % 2000; d = (t < 90 || (t > 220 && t < 310)) ? 1 : 0.06f; break; }  // Doppelblitz
    default: d = 1;                                                                // an
  }
  if (now < btnAckUntil) d = 0;   // Klick quittiert: kurz aus
  ledcWrite(CH_BTN_LED, (uint32_t)(d * 255 * cfg.btnBright / 100));
}

// ---------------------------------------------------------------------
//  WLAN
// ---------------------------------------------------------------------
static void startAP(bool open = false) {
  if (apActive && open == apOpen) return;
  if (apActive) { dns.stop(); WiFi.softAPdisconnect(true); }
  WiFi.mode(cfg.ssid.length() ? WIFI_AP_STA : WIFI_AP);
  bool pw = !open && cfg.apPass.length() >= 8;
  WiFi.softAP(apSsid.c_str(), pw ? cfg.apPass.c_str() : nullptr);
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
  apOpen = !pw;
  Serial.printf("[WLAN] Hotspot \"%s\" aktiv (%s) – http://%s/\n", apSsid.c_str(), pw ? "mit Passwort" : "offen",
                WiFi.softAPIP().toString().c_str());
}

static void stopAP() {
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  apOpen = false;
}

// Encoder-Knopf 3 s: offenen Hotspot einschalten bzw. wieder ausschalten
static void toggleForcedAP() {
  uint32_t now = millis();
  if (apActive) {
    stopAP();
    apUserOff = true;
    if (cfg.ssid.length() && WiFi.status() != WL_CONNECTED) {
      WiFi.setAutoReconnect(true);
      WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
      wstate = W_CONNECTING;
      tWifi = now;
    }
    ledFlashColor = strip.Color(255, 0, 0);
    Serial.println("[WLAN] Hotspot per Knopf ausgeschaltet");
    toast("Hotspot ausgeschaltet");
  } else {
    apUserOff = false;
    startAP(true);
    ledFlashColor = strip.Color(0, 90, 255);
    toast("Hotspot ohne Passwort gestartet");
  }
  ledFlashUntil = now + 900;
}

static void wifiBegin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof(buf), "LiquidHarvest-%02X%02X", mac[4], mac[5]);
  apSsid = buf;
  WiFi.persistent(false);
  WiFi.setHostname(cfg.host.c_str());

  if (cfg.ssid.length()) {
    WiFi.mode(cfg.apAlways ? WIFI_AP_STA : WIFI_STA);
    if (cfg.apAlways) startAP();
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    wstate = W_CONNECTING;
    tWifi = millis();
    Serial.printf("[WLAN] Verbinde mit \"%s\" ...\n", cfg.ssid.c_str());
  } else {
    startAP();
    wstate = W_AP;
  }
  if (MDNS.begin(cfg.host.c_str())) MDNS.addService("http", "tcp", 80);
}

static void wifiLoop() {
  uint32_t now = millis();
  if (apActive) dns.processNextRequest();

  switch (wstate) {
    case W_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        wstate = W_STA;
        Serial.printf("[WLAN] Verbunden: http://%s/  bzw. http://%s.local/\n",
                      WiFi.localIP().toString().c_str(), cfg.host.c_str());
      } else if (now - tWifi > 20000 && apUserOff) {
        tWifi = now;   // Hotspot wurde per Knopf ausgeschaltet: weiter nur das Heimnetz versuchen
      } else if (now - tWifi > 20000) {
        Serial.println("[WLAN] Heimnetz nicht erreichbar -> Hotspot");
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false);
        startAP();
        if (!cfg.apAlways) WiFi.mode(WIFI_AP);
        wstate = W_AP;
        tStaRetry = now;
      }
      break;

    case W_STA:
      break;   // AutoReconnect erledigt Verbindungsabbrüche

    case W_AP:
      // Heimnetz regelmäßig erneut versuchen (z. B. Router nach Stromausfall später da)
      if (cfg.ssid.length() && !cfg.apAlways && !apOpen) {
        if (!staRetrying && now - tStaRetry > 120000 && WiFi.softAPgetStationNum() == 0) {
          WiFi.mode(WIFI_AP_STA);
          WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
          staRetrying = true;
          tStaRetry = now;
        } else if (staRetrying) {
          if (WiFi.status() == WL_CONNECTED) {
            staRetrying = false;
            stopAP();
            WiFi.setAutoReconnect(true);
            wstate = W_STA;
            Serial.printf("[WLAN] Verbunden: http://%s/\n", WiFi.localIP().toString().c_str());
          } else if (now - tStaRetry > 15000) {
            WiFi.disconnect(false);
            WiFi.mode(WIFI_AP);
            staRetrying = false;
            tStaRetry = now;
          }
        }
      } else if (cfg.ssid.length() && WiFi.status() == WL_CONNECTED) {
        wstate = W_STA;
      }
      break;
  }

  if (scanPending) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      JsonDocument d;
      d["t"] = "scan";
      JsonArray a = d["list"].to<JsonArray>();
      for (int i = 0; i < n && i < 25; i++) {
        JsonObject o = a.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["lock"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      }
      WiFi.scanDelete();
      scanPending = false;
      wsSend(d);
    } else if (n == WIFI_SCAN_FAILED) {
      scanPending = false;
    }
  }
}

// Improv-Callbacks (Web-Flash-Tool)
static bool improvConnect(const String& ssid, const String& pass) {
  stopAll("wifi_setup");
  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(false);
    if (apActive) WiFi.mode(WIFI_AP);
    return false;
  }
  cfg.ssid = ssid;
  cfg.pass = pass;
  settingsSave();
  if (!cfg.apAlways) stopAP();
  WiFi.setAutoReconnect(true);
  wstate = W_STA;
  return true;
}
static bool improvConnected() { return WiFi.status() == WL_CONNECTED; }
static String improvUrl() { return "http://" + WiFi.localIP().toString() + "/"; }
static String improvName() { return cfg.host; }

// ---------------------------------------------------------------------
//  Befehle (WebSocket + REST landen beide hier, in der Hauptschleife)
// ---------------------------------------------------------------------
static void pushCmd(const String& s) {
  xSemaphoreTake(cmdMutex, portMAX_DELAY);
  if (cmdQueue.size() < 20) cmdQueue.push_back(s);
  xSemaphoreGive(cmdMutex);
}

static void handleCmd(const String& msg) {
  JsonDocument d;
  if (deserializeJson(d, msg)) return;
  String c = d["c"] | "";
  int p = d["p"] | 0;

  if (c == "pour") {
    if (enqueue(p - 1, d["ml"] | cfg.shotMl, SRC_WEB)) toast("Glas " + String(p) + " ist eingereiht");
  } else if (c == "autofill") {
    int n = autofill(d["ml"] | cfg.shotMl);
    if (n) toast(String(n) + (n == 1 ? " Glas wird" : " Gläser werden") + " befüllt");
  } else if (c == "stop") {
    stopAll("web_stop");
  } else if (c == "aim") {
    if (!pourBusy()) aim(p);
  } else if (c == "servo") {            // freier Winkel zum Einstellen
    if (!pourBusy()) servoTgt = constrain((float)(d["a"] | 90.0f), 0.0f, 180.0f);
  } else if (c == "prime") {
    startPrime((uint32_t)((d["s"] | 3.0f) * 1000), false);
  } else if (c == "tare") {
    if (p >= 1 && p <= NUM_PLACES) Scales::tare(p - 1);
    else for (int i = 0; i < NUM_PLACES; i++) if (places[i].st == P_NONE) Scales::tare(i);
    settingsSave();
    toast("Tariert");
  } else if (c == "calib") {
    float g = d["g"] | 0.0f;
    if (p >= 1 && p <= NUM_PLACES && Scales::calibrate(p - 1, g)) {
      settingsSave(); sendCfg(); toast("Waage " + String(p) + " kalibriert");
    } else toast("Kalibrierung fehlgeschlagen – erst tarieren, dann Gewicht auflegen");
  } else if (c == "getCfg") {
    sendCfg();
  } else if (c == "setCfg") {
    JsonObject o = d["cfg"];
    // WLAN-Felder nur über "wifi"
    o.remove("ssid"); o.remove("pass"); o.remove("apPass"); o.remove("host"); o.remove("apAlways");
    settingsFromJson(o);
    settingsSave();
    applyLedCount();
    if (!pourBusy()) aim(aimIdx);
    sendCfg();
    toast("Gespeichert");
  } else if (c == "wifi") {
    JsonObject o = d.as<JsonObject>();
    const char* np = o["pass"] | "";
    if (!np[0]) o.remove("pass");          // leeres Feld = altes Passwort behalten
    settingsFromJson(o);
    if (!o["clearPass"].isNull() && o["clearPass"].as<bool>()) cfg.pass = "";
    settingsSave();
    toast("WLAN gespeichert – Neustart ...");
    rebootAt = millis() + 1500;
  } else if (c == "scan") {
    if (!scanPending) { WiFi.scanNetworks(true); scanPending = true; }
  } else if (c == "ledPreview") {       // {"c":"ledPreview","col":"#FF0000","lvl":80,"fx":1}
    const char* h = d["col"] | "#FFFFFF";
    ledPrevCol = strtoul(h + (h[0] == '#'), nullptr, 16) & 0xFFFFFF;
    ledPrevLvl = constrain((int)(d["lvl"] | 100), 0, 100);
    ledPrevFx = constrain((int)(d["fx"] | 0), 0, 3);
    ledPrevUntil = millis() + 3000;
  } else if (c == "ledTest") {          // {"c":"ledTest","leds":[2,3]} – LEDs kurz weiß blinken lassen
    ledTestN = 0;
    for (JsonVariant v : d["leds"].as<JsonArray>()) if (ledTestN < sizeof(ledTest)) ledTest[ledTestN++] = v.as<int>();
    ledTestUntil = millis() + 2500;
  } else if (c == "reboot") {
    rebootAt = millis() + 500;
  } else if (c == "reapplySetup") {
    Preferences pr; pr.begin("shdef", false); pr.clear(); pr.end();
    toast("Standard-Einrichtung wird neu geladen – Neustart ...");
    rebootAt = millis() + 1200;
  } else if (c == "factory") {
    settingsFactoryReset();
    toast("Werkseinstellungen – Neustart ...");
    rebootAt = millis() + 1500;
  }
}

static void processCmds() {
  std::vector<String> local;
  xSemaphoreTake(cmdMutex, portMAX_DELAY);
  local.swap(cmdQueue);
  xSemaphoreGive(cmdMutex);
  for (auto& s : local) handleCmd(s);
}

// ---------------------------------------------------------------------
//  Status an alle Clients
// ---------------------------------------------------------------------
static void buildState(String& out) {
  JsonDocument d;
  d["t"] = "st";
  d["aim"] = aimIdx;
  d["ang"] = roundf(servoCur * 10) / 10;
  d["ph"] = PHASE_NAMES[pour.ph];
  d["pp"] = pour.p + 1;
  d["tgt"] = roundf(pour.ml * 10) / 10;
  d["got"] = roundf(pour.got / cfg.density * 10) / 10;
  d["q"] = jobs.size();
  d["busy"] = pourBusy();
  d["pump"] = pumpPct;
  JsonArray a = d["pl"].to<JsonArray>();
  uint32_t now = millis();
  for (int i = 0; i < NUM_PLACES; i++) {
    JsonObject o = a.add<JsonObject>();
    const Place& pl = places[i];
    o["s"] = pl.st;
    o["ok"] = Scales::ok(i);
    o["en"] = (bool)(cfg.enabledMask & (1 << i));
    o["g"] = roundf(Scales::gs(i) * 10) / 10;
    o["raw"] = Scales::raw(i);
    float content = pl.st == P_NONE ? 0 : max(0.0f, (Scales::gs(i) - pl.tare) / cfg.density);
    o["ml"] = roundf(content * 10) / 10;
    o["err"] = now < pl.errUntil;
  }
  JsonObject w = d["wifi"].to<JsonObject>();
  w["sta"] = WiFi.status() == WL_CONNECTED;
  w["ssid"] = cfg.ssid;
  w["ip"] = WiFi.localIP().toString();
  w["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  w["ap"] = apActive;
  w["apOpen"] = apOpen;
  w["apSsid"] = apSsid;
  w["host"] = cfg.host;
  d["up"] = now / 1000;
  serializeJson(d, out);
}

static void stateLoop() {
  static uint32_t last = 0, lastFull = 0;
  static String lastSent;
  uint32_t now = millis();
  if (now - last < 150) return;
  last = now;
  String s;
  buildState(s);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  stateCache = s;
  xSemaphoreGive(stateMutex);
  if (ws.count() && (s != lastSent || now - lastFull > 2000)) {
    ws.textAll(s);
    lastSent = s;
    lastFull = now;
  }
}

// ---------------------------------------------------------------------
//  Webserver
// ---------------------------------------------------------------------
static void onWs(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    pushCmd("{\"c\":\"getCfg\"}");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      pushCmd(String((const char*)data, len));
    }
  }
}

static void sendIndex(AsyncWebServerRequest* r) {
  AsyncWebServerResponse* res = r->beginResponse(200, "text/html", WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-cache");
  r->send(res);
}

static void webBegin() {
  ws.onEvent(onWs);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, sendIndex);

  // Eigene Medien im LittleFS: f=img (Drescher-Bild) oder f=vid (Rohr-Video als Bildstreifen)
  auto serveMedia = [](AsyncWebServerRequest* r, const char* base) {
    String path = String("/") + base + ".bin", tpath = String("/") + base + ".type";
    if (!fsOk || !LittleFS.exists(path)) { r->send(404, "text/plain", "nicht vorhanden"); return; }
    String type = "image/webp";
    File t = LittleFS.open(tpath, "r");
    if (t) { type = t.readString(); t.close(); }
    AsyncWebServerResponse* res = r->beginResponse(LittleFS, path, type);
    res->addHeader("Cache-Control", "max-age=31536000");   // URL enthält ?v=Version
    r->send(res);
  };
  server.on("/combine.img", HTTP_GET, [serveMedia](AsyncWebServerRequest* r) { serveMedia(r, "img"); });
  server.on("/video.img", HTTP_GET, [serveMedia](AsyncWebServerRequest* r) { serveMedia(r, "vid"); });
  server.on("/upload", HTTP_POST,
    [](AsyncWebServerRequest* r) {
      if (!fsOk) { r->send(500, "text/plain", "Dateisystem nicht verfügbar"); return; }
      if (imgTooBig) { r->send(413, "text/plain", "Datei zu groß für den Speicher"); return; }
      r->send(200, "text/plain", "OK");
    },
    [](AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
      if (!fsOk) return;
      static String base;
      static size_t maxSize;
      if (!index) {
        base = (r->hasParam("f") && r->getParam("f")->value() == "vid") ? "vid" : "img";
        // Alte Datei sofort löschen – sonst passen alte + neue Datei nicht gleichzeitig in den Speicher
        LittleFS.remove("/" + base + ".tmp");
        LittleFS.remove("/" + base + ".bin");
        LittleFS.remove("/" + base + ".type");
        size_t freeB = LittleFS.totalBytes() - LittleFS.usedBytes();
        maxSize = freeB > 16384 ? freeB - 16384 : 0;
        if (base == "img" && maxSize > IMG_MAX) maxSize = IMG_MAX;
        imgTooBig = false;
        imgFile = LittleFS.open("/" + base + ".bin", "w");
        Serial.printf("[Upload] %s: frei %u Bytes\n", base.c_str(), (unsigned)maxSize);
      }
      if (index + len > maxSize) imgTooBig = true;
      if (imgFile && !imgTooBig && imgFile.write(data, len) != len) imgTooBig = true;   // Speicher voll
      if (final && imgFile) {
        imgFile.close();
        if (imgTooBig) {
          LittleFS.remove("/" + base + ".bin");
          Serial.printf("[Upload] %s zu groß (%u Bytes)\n", base.c_str(), (unsigned)(index + len));
          if (base == "img") imgChanged = true; else vidChanged = true;
          return;
        }
        File t = LittleFS.open("/" + base + ".type", "w");
        if (t) { t.print(filename.endsWith(".png") ? "image/png" : filename.endsWith(".jpg") ? "image/jpeg" : "image/webp"); t.close(); }
        Serial.printf("[Upload] %s gespeichert (%u Bytes)\n", base.c_str(), (unsigned)(index + len));
        if (base == "img") imgChanged = true; else vidChanged = true;
      }
    });
  server.on("/upload/delete", HTTP_POST, [](AsyncWebServerRequest* r) {
    String base = (r->hasParam("f") && r->getParam("f")->value() == "vid") ? "vid" : "img";
    if (fsOk) { LittleFS.remove("/" + base + ".bin"); LittleFS.remove("/" + base + ".type"); }
    if (base == "img") imgChanged = true; else vidChanged = true;
    r->send(200, "text/plain", "OK");
  });
  server.on("/fs", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", fsOk ? "{\"total\":" + String(LittleFS.totalBytes()) + ",\"used\":" + String(LittleFS.usedBytes()) + "}" : "{\"total\":0,\"used\":0}");
  });

  // REST-API (z. B. für Home Assistant)
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* r) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    String s = stateCache;
    xSemaphoreGive(stateMutex);
    r->send(200, "application/json", s);
  });
  server.on("/api/pour", HTTP_POST, [](AsyncWebServerRequest* r) {
    int p = r->hasParam("p", true) ? r->getParam("p", true)->value().toInt() : (r->hasParam("p") ? r->getParam("p")->value().toInt() : 0);
    float ml = r->hasParam("ml", true) ? r->getParam("ml", true)->value().toFloat() : (r->hasParam("ml") ? r->getParam("ml")->value().toFloat() : cfg.shotMl);
    pushCmd("{\"c\":\"pour\",\"p\":" + String(p) + ",\"ml\":" + String(ml, 1) + "}");
    r->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/autofill", HTTP_POST, [](AsyncWebServerRequest* r) {
    float ml = r->hasParam("ml", true) ? r->getParam("ml", true)->value().toFloat() : (r->hasParam("ml") ? r->getParam("ml")->value().toFloat() : cfg.shotMl);
    pushCmd("{\"c\":\"autofill\",\"ml\":" + String(ml, 1) + "}");
    r->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest* r) {
    pushCmd("{\"c\":\"stop\"}");
    r->send(200, "application/json", "{\"ok\":true}");
  });

  // OTA-Update (firmware.bin über die Weboberfläche)
  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest* r) {
      bool ok = !Update.hasError();
      AsyncWebServerResponse* res = r->beginResponse(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
      res->addHeader("Connection", "close");
      r->send(res);
      if (ok) rebootAt = millis() + 1000;
      otaRunning = false;
    },
    [](AsyncWebServerRequest*, const String&, size_t index, uint8_t* data, size_t len, bool final) {
      if (!index) {
        otaRunning = true;
        ledcWrite(CH_PUMP, 0);
        Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      }
      if (!Update.hasError()) Update.write(data, len);
      if (final) Update.end(true);
    });

  // Captive Portal: im Hotspot-Modus alles auf die Startseite umleiten
  server.onNotFound([](AsyncWebServerRequest* r) {
    if (apActive) {
      String h = r->host();
      if (h != WiFi.softAPIP().toString() && h != WiFi.localIP().toString() && !h.endsWith(".local")) {
        r->redirect("http://" + WiFi.softAPIP().toString() + "/");
        return;
      }
      sendIndex(r);
      return;
    }
    r->send(404, "text/plain", "Not found");
  });

  server.begin();
}

// ---------------------------------------------------------------------
//  Standard-Einrichtung (mitgeflasht aus setup/default-setup.json)
// ---------------------------------------------------------------------
static String setupInfo = "keine";
static String setupInfoStr() { return setupInfo; }
static void applyDefaultSetup() {
  if (!fsOk || !LittleFS.exists("/setup.json")) { setupInfo = "keine mitgeflasht"; return; }
  File f = LittleFS.open("/setup.json", "r");
  JsonDocument d;
  DeserializationError err = deserializeJson(d, f);
  f.close();
  if (err) return;
  String ver = d["ver"] | "";
  Preferences pr;
  pr.begin("shdef", false);
  if (pr.getString("ver", "") == ver) { pr.end(); setupInfo = "aktiv (" + ver + ")"; return; }   // schon übernommen
  settingsFromJson(d["cfg"].as<JsonObjectConst>());
  if (LittleFS.exists("/img.bin")) cfg.imgVer = esp_random();
  settingsSave();
  pr.putString("ver", ver);
  pr.end();
  setupInfo = "neu übernommen (" + ver + ")";
  Serial.printf("[Setup] Standard-Einrichtung \"%s\" übernommen\n", (const char*)(d["name"] | ""));
}

// ---------------------------------------------------------------------
//  Setup / Loop
// ---------------------------------------------------------------------
void setup() {
  // Pumpe zuerst sicher aus
  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_PUMP, LOW);

  Serial.begin(115200);
  delay(50);
  Serial.printf("\n\n=== %s v%s ===\n", FW_NAME, FW_VERSION);

  cmdMutex = xSemaphoreCreateMutex();
  stateMutex = xSemaphoreCreateMutex();

  settingsLoad();
  fsOk = LittleFS.begin(true);   // Partition "spiffs" (896 kB) für Drescher-Bild und Rohr-Video
  Serial.printf("[FS] LittleFS %s\n", fsOk ? "bereit" : "FEHLER");
  applyDefaultSetup();
  Serial.printf("[Setup] Standard-Einrichtung: %s | Darstellung %u Zeichen | Park %.1f°, Glas 1 %.1f°, Glas 6 %.1f°\n",
                setupInfo.c_str(), (unsigned)cfg.ui.length(), cfg.angPark, cfg.ang[0], cfg.ang[NUM_PLACES - 1]);

  ledcSetup(CH_PUMP, 1000, 8);
  ledcAttachPin(PIN_PUMP, CH_PUMP);
  pumpSet(0);

  ledcSetup(CH_SERVO, 50, 16);
  ledcAttachPin(PIN_SERVO, CH_SERVO);
  servoCur = servoTgt = cfg.angPark;
  servoWrite(servoCur);

  ledcSetup(CH_BTN_LED, 5000, 8);
  ledcAttachPin(PIN_BTN_LED, CH_BTN_LED);

  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT), encIsr, CHANGE);

  strip.begin();
  strip.clear();
  strip.show();                 // ganze mögliche Kette einmal löschen
  applyLedCount();

  Scales::begin();

  wifiBegin();
  webBegin();
  Improv::begin({improvConnect, improvConnected, improvUrl, improvName});
}

void loop() {
  if (otaRunning) { pumpSet(0); jobs.clear(); pour.ph = PH_IDLE; delay(5); return; }

  Scales::loop();
  placesLoop();
  inputLoop();
  processCmds();
  pourLoop(btn.stable);
  servoLoop();
  ledsLoop();
  wifiLoop();
  stateLoop();
  Improv::loop();
  ws.cleanupClients();

  if (imgChanged) {
    imgChanged = false;
    bool has = fsOk && LittleFS.exists("/img.bin");
    cfg.imgVer = has ? esp_random() : 0;
    if (!has) cfg.imgOn = false;
    settingsSave();
    sendCfg();
  }
  if (vidChanged) {
    vidChanged = false;
    JsonDocument d;
    d["t"] = "vid";
    d["has"] = fsOk && LittleFS.exists("/vid.bin");
    d["ver"] = esp_random();
    wsSend(d);
  }

  if (rebootAt && millis() > rebootAt) {
    pumpSet(0);
    delay(100);
    ESP.restart();
  }
}
