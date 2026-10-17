#include "settings.h"
#include <Preferences.h>

Settings cfg;
static Preferences prefs;

template <typename T>
static void rd(JsonObjectConst o, const char* k, T& v) {
  JsonVariantConst x = o[k];
  if (!x.isNull()) v = x.as<T>();
}

static void clampAll() {
  cfg.shotMl     = constrain(cfg.shotMl, 1.0f, 100.0f);
  cfg.doubleMl   = constrain(cfg.doubleMl, 1.0f, 100.0f);
  cfg.density    = constrain(cfg.density, 0.5f, 1.5f);
  cfg.pwmFast    = constrain(cfg.pwmFast, (uint8_t)10, (uint8_t)100);
  cfg.pwmSlow    = constrain(cfg.pwmSlow, (uint8_t)5, (uint8_t)100);
  cfg.slowBefore = constrain(cfg.slowBefore, 0.0f, 30.0f);
  cfg.stopBefore = constrain(cfg.stopBefore, 0.0f, 10.0f);
  cfg.noFlowMs   = constrain(cfg.noFlowMs, (uint32_t)500, (uint32_t)30000);
  cfg.maxPourMs  = constrain(cfg.maxPourMs, (uint32_t)2000, (uint32_t)120000);
  cfg.settleMs   = constrain(cfg.settleMs, (uint32_t)0, (uint32_t)3000);
  cfg.glassMin   = constrain(cfg.glassMin, 0.5f, 200.0f);
  cfg.stableMs   = constrain(cfg.stableMs, (uint32_t)100, (uint32_t)5000);
  cfg.angPark    = constrain(cfg.angPark, 0.0f, 180.0f);
  for (int i = 0; i < NUM_PLACES; i++) {
    cfg.ang[i] = constrain(cfg.ang[i], 0.0f, 180.0f);
    if (fabsf(cfg.factor[i]) < 1.0f) cfg.factor[i] = 420.0f;
  }
  cfg.servoSpeed = constrain(cfg.servoSpeed, 10.0f, 720.0f);
  cfg.servoMinUs = constrain(cfg.servoMinUs, (uint16_t)300, (uint16_t)1500);
  cfg.servoMaxUs = constrain(cfg.servoMaxUs, (uint16_t)1500, (uint16_t)2800);
  cfg.ledBright  = constrain(cfg.ledBright, (uint8_t)1, (uint8_t)100);
  cfg.encDiv     = constrain(cfg.encDiv, (uint8_t)1, (uint8_t)8);
  cfg.ledCount   = constrain(cfg.ledCount, (uint8_t)1, (uint8_t)MAX_LEDS);
  for (int i = 0; i < NUM_PLACES; i++) {          // ungültige Nummern entfernen
    uint8_t n = 0;
    for (int k = 0; k < cfg.ledMapN[i] && k < MAX_LEDS_PER_GLASS; k++) {
      uint8_t v = cfg.ledMap[i][k];
      if (v >= 1 && v <= cfg.ledCount) cfg.ledMap[i][n++] = v;
    }
    cfg.ledMapN[i] = n;
  }
  cfg.enabledMask &= 0x3F;
  cfg.imgW = constrain(cfg.imgW, 40.0f, 900.0f);
  cfg.augW = constrain(cfg.augW, 3.0f, 30.0f);
  cfg.augPX = constrain(cfg.augPX, -300.0f, 300.0f);
  cfg.augPY = constrain(cfg.augPY, -300.0f, 300.0f);
  cfg.augPZ = constrain(cfg.augPZ, 40.0f, 300.0f);
  cfg.augEZ = constrain(cfg.augEZ, 60.0f, 300.0f);
  if (cfg.ui.length() > 2000 || cfg.ui.length() < 2) cfg.ui = "{}";
  if (cfg.augCol.length() != 7 || cfg.augCol[0] != '#') cfg.augCol = "#2f8f4e";
  cfg.imgX = constrain(cfg.imgX, -700.0f, 700.0f);
  cfg.imgY = constrain(cfg.imgY, -700.0f, 700.0f);
  if (cfg.host.length() == 0) cfg.host = "liquidharvest";
  if (cfg.apPass.length() > 0 && cfg.apPass.length() < 8) cfg.apPass = "prost1234";
}

void settingsToJson(JsonObject o) {
  o["shotMl"] = cfg.shotMl;       o["doubleMl"] = cfg.doubleMl;
  o["density"] = cfg.density;     o["pwmFast"] = cfg.pwmFast;
  o["pwmSlow"] = cfg.pwmSlow;     o["slowBefore"] = cfg.slowBefore;
  o["stopBefore"] = cfg.stopBefore;
  o["noFlowMs"] = cfg.noFlowMs;   o["maxPourMs"] = cfg.maxPourMs;
  o["settleMs"] = cfg.settleMs;   o["parkAfter"] = cfg.parkAfter;
  o["glassMin"] = cfg.glassMin;   o["stableMs"] = cfg.stableMs;
  o["angPark"] = cfg.angPark;
  JsonArray a = o["ang"].to<JsonArray>();
  for (int i = 0; i < NUM_PLACES; i++) a.add(cfg.ang[i]);
  o["servoSpeed"] = cfg.servoSpeed;
  o["servoMinUs"] = cfg.servoMinUs; o["servoMaxUs"] = cfg.servoMaxUs;
  o["ledBright"] = cfg.ledBright; o["encDiv"] = cfg.encDiv;
  o["ledCount"] = cfg.ledCount;
  JsonArray lm = o["ledMap"].to<JsonArray>();
  for (int i = 0; i < NUM_PLACES; i++) {
    JsonArray g = lm.add<JsonArray>();
    for (int k = 0; k < cfg.ledMapN[i]; k++) g.add(cfg.ledMap[i][k]);
  }
  o["encReverse"] = cfg.encReverse;
  o["imgOn"] = cfg.imgOn; o["imgW"] = cfg.imgW; o["imgX"] = cfg.imgX; o["imgY"] = cfg.imgY; o["imgVer"] = cfg.imgVer;
  o["augCol"] = cfg.augCol; o["augW"] = cfg.augW; o["augPX"] = cfg.augPX; o["augPY"] = cfg.augPY;
  o["augPZ"] = cfg.augPZ; o["augEZ"] = cfg.augEZ; o["augRiser"] = cfg.augRiser;
  o["ui"] = cfg.ui;
  JsonArray off = o["offset"].to<JsonArray>();
  JsonArray fac = o["factor"].to<JsonArray>();
  for (int i = 0; i < NUM_PLACES; i++) { off.add(cfg.offset[i]); fac.add(cfg.factor[i]); }
  o["enabledMask"] = cfg.enabledMask;
  o["ssid"] = cfg.ssid;  o["host"] = cfg.host;
  o["apAlways"] = cfg.apAlways;
  o["hasPass"] = cfg.pass.length() > 0;
  o["hasApPass"] = cfg.apPass.length() > 0;
}

void settingsFromJson(JsonObjectConst o) {
  rd(o, "shotMl", cfg.shotMl);       rd(o, "doubleMl", cfg.doubleMl);
  rd(o, "density", cfg.density);     rd(o, "pwmFast", cfg.pwmFast);
  rd(o, "pwmSlow", cfg.pwmSlow);     rd(o, "slowBefore", cfg.slowBefore);
  rd(o, "stopBefore", cfg.stopBefore);
  rd(o, "noFlowMs", cfg.noFlowMs);   rd(o, "maxPourMs", cfg.maxPourMs);
  rd(o, "settleMs", cfg.settleMs);   rd(o, "parkAfter", cfg.parkAfter);
  rd(o, "glassMin", cfg.glassMin);   rd(o, "stableMs", cfg.stableMs);
  rd(o, "angPark", cfg.angPark);
  JsonArrayConst a = o["ang"];
  if (!a.isNull()) for (int i = 0; i < NUM_PLACES && i < (int)a.size(); i++) cfg.ang[i] = a[i].as<float>();
  rd(o, "servoSpeed", cfg.servoSpeed);
  rd(o, "servoMinUs", cfg.servoMinUs); rd(o, "servoMaxUs", cfg.servoMaxUs);
  rd(o, "ledBright", cfg.ledBright);   rd(o, "encDiv", cfg.encDiv);
  rd(o, "ledCount", cfg.ledCount);
  JsonArrayConst lm = o["ledMap"];
  if (!lm.isNull()) {
    for (int i = 0; i < NUM_PLACES; i++) {
      JsonArrayConst g = lm[i];
      cfg.ledMapN[i] = 0;
      if (g.isNull()) continue;
      for (JsonVariantConst v : g) {
        if (cfg.ledMapN[i] >= MAX_LEDS_PER_GLASS) break;
        int n = v.as<int>();
        if (n >= 1 && n <= MAX_LEDS) cfg.ledMap[i][cfg.ledMapN[i]++] = (uint8_t)n;
      }
    }
  }
  rd(o, "encReverse", cfg.encReverse);
  rd(o, "imgOn", cfg.imgOn); rd(o, "imgW", cfg.imgW); rd(o, "imgX", cfg.imgX); rd(o, "imgY", cfg.imgY); rd(o, "imgVer", cfg.imgVer);
  rd(o, "augCol", cfg.augCol); rd(o, "augW", cfg.augW); rd(o, "augPX", cfg.augPX); rd(o, "augPY", cfg.augPY);
  rd(o, "augPZ", cfg.augPZ); rd(o, "augEZ", cfg.augEZ); rd(o, "augRiser", cfg.augRiser);
  rd(o, "ui", cfg.ui);
  JsonArrayConst off = o["offset"];
  if (!off.isNull()) for (int i = 0; i < NUM_PLACES && i < (int)off.size(); i++) cfg.offset[i] = off[i].as<int32_t>();
  JsonArrayConst fac = o["factor"];
  if (!fac.isNull()) for (int i = 0; i < NUM_PLACES && i < (int)fac.size(); i++) cfg.factor[i] = fac[i].as<float>();
  rd(o, "enabledMask", cfg.enabledMask);
  rd(o, "ssid", cfg.ssid);   rd(o, "host", cfg.host);
  rd(o, "apAlways", cfg.apAlways);
  // Passwörter nur, wenn explizit mitgeschickt
  rd(o, "pass", cfg.pass);   rd(o, "apPass", cfg.apPass);
  clampAll();
}

void settingsSave() {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  settingsToJson(o);
  o.remove("hasPass"); o.remove("hasApPass");
  o["pass"] = cfg.pass;
  o["apPass"] = cfg.apPass;
  String s;
  serializeJson(d, s);
  prefs.begin("sh", false);
  prefs.putString("cfg", s);
  prefs.end();
}

void settingsLoad() {
  prefs.begin("sh", true);
  String s = prefs.getString("cfg", "");
  prefs.end();
  if (s.length()) {
    JsonDocument d;
    if (!deserializeJson(d, s)) settingsFromJson(d.as<JsonObjectConst>());
  }
  clampAll();
}

void settingsFactoryReset() {
  prefs.begin("sh", false);
  prefs.clear();
  prefs.end();
  prefs.begin("shdef", false);   // Standard-Einrichtung beim nächsten Start erneut übernehmen
  prefs.clear();
  prefs.end();
  cfg = Settings();
}
