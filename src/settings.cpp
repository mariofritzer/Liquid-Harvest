#include "settings.h"
#include <Preferences.h>
#include <memory>

Settings cfg;
static Preferences prefs;

template <typename T>
static void rd(JsonObjectConst o, const char* k, T& v) {
  JsonVariantConst x = o[k];
  if (!x.isNull()) v = x.as<T>();
}

static void profFromCfg(DrinkProfile& p) {
  p.shotMl = cfg.shotMl; p.doubleMl = cfg.doubleMl; p.density = cfg.density;
  p.pwmFast = cfg.pwmFast; p.pwmSlow = cfg.pwmSlow; p.slowBefore = cfg.slowBefore; p.stopBefore = cfg.stopBefore; p.dripMs = cfg.dripMs;
}
static void profToCfg(const DrinkProfile& p) {
  cfg.shotMl = p.shotMl; cfg.doubleMl = p.doubleMl; cfg.density = p.density;
  cfg.pwmFast = p.pwmFast; cfg.pwmSlow = p.pwmSlow; cfg.slowBefore = p.slowBefore; cfg.stopBefore = p.stopBefore; cfg.dripMs = p.dripMs;
}
static String cleanName(String n) {
  n.trim(); n.replace("\"", ""); n.replace("<", ""); n.replace(">", "");
  if (n.length() > 20) n = n.substring(0, 20);
  return n;
}

static void clampAll() {
  cfg.shotMl     = constrain(cfg.shotMl, 1.0f, 100.0f);
  cfg.doubleMl   = constrain(cfg.doubleMl, 1.0f, 100.0f);
  cfg.density    = constrain(cfg.density, 0.5f, 1.5f);
  cfg.pwmFast    = constrain(cfg.pwmFast, (uint8_t)10, (uint8_t)100);
  cfg.pwmSlow    = constrain(cfg.pwmSlow, (uint8_t)5, (uint8_t)100);
  cfg.slowBefore = constrain(cfg.slowBefore, 0.0f, 30.0f);
  cfg.stopBefore = constrain(cfg.stopBefore, 0.0f, 10.0f);
  cfg.cleanDays  = min(cfg.cleanDays, (uint8_t)60);
  cfg.pumpDriver = min(cfg.pumpDriver, (uint8_t)1);
  cfg.retractMs  = min(cfg.retractMs, (uint16_t)2000);
  cfg.retractPwm = constrain(cfg.retractPwm, (uint8_t)10, (uint8_t)100);
  cfg.cleanRinseS = constrain(cfg.cleanRinseS, (uint16_t)3, (uint16_t)120);
  cfg.cleanDryS  = constrain(cfg.cleanDryS, (uint16_t)3, (uint16_t)120);
  if (cfg.cleanPos > NUM_PLACES) cfg.cleanPos = 0;
  cfg.beaconCol &= 0xFFFFFF; cfg.beaconLvl = min(cfg.beaconLvl, (uint8_t)100);
  cfg.noFlowMs   = constrain(cfg.noFlowMs, (uint32_t)500, (uint32_t)30000);
  cfg.maxPourMs  = constrain(cfg.maxPourMs, (uint32_t)2000, (uint32_t)120000);
  cfg.settleMs   = constrain(cfg.settleMs, (uint32_t)0, (uint32_t)3000);
  cfg.dripMs     = min(cfg.dripMs, (uint16_t)10000);
  if (cfg.beaconFx > 5) cfg.beaconFx = 0;
  cfg.glassMin   = constrain(cfg.glassMin, 0.1f, 200.0f);
  cfg.stableMs   = constrain(cfg.stableMs, (uint32_t)100, (uint32_t)5000);
  cfg.angOff     = constrain(cfg.angOff, 0.0f, 175.0f);
  cfg.angMax     = constrain(cfg.angMax, 5.0f, 180.0f - cfg.angOff);
  cfg.angPark    = constrain(cfg.angPark, 0.0f, cfg.angMax);
  for (int i = 0; i < NUM_PLACES; i++) {
    cfg.ang[i] = constrain(cfg.ang[i], 0.0f, cfg.angMax);
    if (fabsf(cfg.factor[i]) < 1.0f) cfg.factor[i] = 420.0f;
  }
  cfg.servoSpeed = constrain(cfg.servoSpeed, 10.0f, 720.0f);
  cfg.servoMinUs = constrain(cfg.servoMinUs, (uint16_t)300, (uint16_t)1500);
  cfg.servoMaxUs = constrain(cfg.servoMaxUs, (uint16_t)1500, (uint16_t)2800);
  cfg.ledBright  = constrain(cfg.ledBright, (uint8_t)1, (uint8_t)100);
  cfg.encDiv     = constrain(cfg.encDiv, (uint8_t)1, (uint8_t)8);
  cfg.ledCount   = constrain(cfg.ledCount, (uint8_t)1, (uint8_t)MAX_LEDS);
  cfg.servoHoldMs = constrain(cfg.servoHoldMs, (uint16_t)100, (uint16_t)5000);
  cfg.btnBright  = constrain(cfg.btnBright, (uint8_t)5, (uint8_t)100);
  for (int i = 0; i < 7; i++) {
    cfg.ledCol[i] &= 0xFFFFFF;
    cfg.ledLvl[i] = min(cfg.ledLvl[i], (uint8_t)100);
    cfg.ledFx[i]  = min(cfg.ledFx[i], (uint8_t)3);
    cfg.btnFx[i]  = min(cfg.btnFx[i], (uint8_t)6);
  }
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
  // Getränke: mindestens eins; das aktive spiegelt immer die aktuellen Dosier-Werte
  if (cfg.profN < 1 || cfg.profN > MAX_PROF) { cfg.profN = 1; cfg.prof[0].name = "Schnaps"; }
  if (cfg.profIdx >= cfg.profN) cfg.profIdx = 0;
  for (int i = 0; i < cfg.profN; i++) if (!cfg.prof[i].name.length()) cfg.prof[i].name = "Getränk " + String(i + 1);
  profFromCfg(cfg.prof[cfg.profIdx]);
}

void profileSelect(int i) {
  if (i < 0 || i >= cfg.profN) return;
  clampAll();                       // aktuelle Werte ins bisherige Getränk
  cfg.profIdx = i;
  profToCfg(cfg.prof[i]);
  settingsSave();
}
bool profileAdd(const String& name) {
  if (cfg.profN >= MAX_PROF) return false;
  clampAll();
  cfg.prof[cfg.profN] = cfg.prof[cfg.profIdx];   // Startwerte vom aktuellen Getränk
  cfg.prof[cfg.profN].name = cleanName(name);
  cfg.profN++;
  profileSelect(cfg.profN - 1);
  return true;
}
bool profileRename(int i, const String& name) {
  if (i < 0 || i >= cfg.profN || !cleanName(name).length()) return false;
  cfg.prof[i].name = cleanName(name); settingsSave(); return true;
}
bool profileDelete(int i) {
  if (cfg.profN <= 1 || i < 0 || i >= cfg.profN) return false;
  clampAll();
  for (int k = i; k < cfg.profN - 1; k++) cfg.prof[k] = cfg.prof[k + 1];
  cfg.profN--;
  int sel = cfg.profIdx == i ? 0 : cfg.profIdx > i ? cfg.profIdx - 1 : cfg.profIdx;
  cfg.profIdx = sel;
  profToCfg(cfg.prof[sel]);
  settingsSave();
  return true;
}

void settingsToJson(JsonObject o) {
  o["shotMl"] = cfg.shotMl;       o["doubleMl"] = cfg.doubleMl;
  o["density"] = cfg.density;     o["pwmFast"] = cfg.pwmFast;
  o["pwmSlow"] = cfg.pwmSlow;     o["slowBefore"] = cfg.slowBefore;
  o["stopBefore"] = cfg.stopBefore;
  o["profIdx"] = cfg.profIdx;
  { JsonArray pa = o["prof"].to<JsonArray>();
    for (int i = 0; i < cfg.profN; i++) {
      const DrinkProfile& p = cfg.prof[i];
      JsonObject x = pa.add<JsonObject>();
      x["n"] = p.name; x["sm"] = p.shotMl; x["dm"] = p.doubleMl; x["d"] = p.density; x["pf"] = p.pwmFast; x["ps"] = p.pwmSlow;
      x["sb"] = p.slowBefore; x["st"] = p.stopBefore; x["dr"] = p.dripMs;
    } }
  o["autoTune"] = cfg.autoTune;
  o["pumpDriver"] = cfg.pumpDriver; o["retractMs"] = cfg.retractMs; o["retractPwm"] = cfg.retractPwm;
  o["cleanShots"] = cfg.cleanShots; o["cleanDays"] = cfg.cleanDays; o["cleanRinseS"] = cfg.cleanRinseS; o["cleanDryS"] = cfg.cleanDryS; o["cleanPos"] = cfg.cleanPos;
  o["shotsSinceClean"] = cfg.shotsSinceClean; o["lastClean"] = cfg.lastClean;
  { JsonArray bm = o["beaconMap"].to<JsonArray>(); for (int k = 0; k < cfg.beaconN; k++) bm.add(cfg.beaconMap[k]);
    char hex[8]; snprintf(hex, sizeof(hex), "#%06X", (unsigned)cfg.beaconCol); o["beaconCol"] = hex; o["beaconLvl"] = cfg.beaconLvl; o["beaconFx"] = cfg.beaconFx; }
  o["noFlowMs"] = cfg.noFlowMs;   o["maxPourMs"] = cfg.maxPourMs;
  o["settleMs"] = cfg.settleMs; o["dripMs"] = cfg.dripMs;   o["parkAfter"] = cfg.parkAfter;
  o["glassMin"] = cfg.glassMin;   o["stableMs"] = cfg.stableMs;
  o["angPark"] = cfg.angPark; o["angOff"] = cfg.angOff; o["angMax"] = cfg.angMax;
  JsonArray a = o["ang"].to<JsonArray>();
  for (int i = 0; i < NUM_PLACES; i++) a.add(cfg.ang[i]);
  o["servoSpeed"] = cfg.servoSpeed;
  o["servoMinUs"] = cfg.servoMinUs; o["servoMaxUs"] = cfg.servoMaxUs;
  o["ledBright"] = cfg.ledBright; o["encDiv"] = cfg.encDiv;
  o["ledCount"] = cfg.ledCount;
  o["servoReverse"] = cfg.servoReverse; o["servoRelease"] = cfg.servoRelease; o["servoHoldMs"] = cfg.servoHoldMs; o["servoHoldPour"] = cfg.servoHoldPour; o["updAuto"] = cfg.updAuto; o["btnBright"] = cfg.btnBright;
  JsonArray lc = o["ledCol"].to<JsonArray>(), ll = o["ledLvl"].to<JsonArray>(), lf = o["ledFx"].to<JsonArray>(), bf = o["btnFx"].to<JsonArray>();
  for (int i = 0; i < 7; i++) {
    char hex[8]; snprintf(hex, sizeof(hex), "#%06X", (unsigned)cfg.ledCol[i]);
    lc.add(hex); ll.add(cfg.ledLvl[i]); lf.add(cfg.ledFx[i]); bf.add(cfg.btnFx[i]);
  }
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
  { JsonArrayConst pa = o["prof"];   // komplette Liste (Import, Standard-Einrichtung, gespeicherte Einstellungen)
    if (!pa.isNull() && pa.size()) {
      cfg.profN = 0;
      for (JsonObjectConst x : pa) {
        if (cfg.profN >= MAX_PROF) break;
        DrinkProfile& p = cfg.prof[cfg.profN++];
        p = DrinkProfile();
        p.name = cleanName(x["n"] | "Getränk"); rd(x, "sm", p.shotMl); rd(x, "dm", p.doubleMl); rd(x, "d", p.density);
        rd(x, "pf", p.pwmFast); rd(x, "ps", p.pwmSlow); rd(x, "sb", p.slowBefore); rd(x, "st", p.stopBefore); rd(x, "dr", p.dripMs);
      }
      rd(o, "profIdx", cfg.profIdx);
      if (cfg.profIdx >= cfg.profN) cfg.profIdx = 0;
      profToCfg(cfg.prof[cfg.profIdx]);   // einzeln mitgeschickte Werte unten überschreiben das dann
    } }
  rd(o, "shotMl", cfg.shotMl);       rd(o, "doubleMl", cfg.doubleMl);
  rd(o, "density", cfg.density);     rd(o, "pwmFast", cfg.pwmFast);
  rd(o, "pwmSlow", cfg.pwmSlow);     rd(o, "slowBefore", cfg.slowBefore);
  rd(o, "stopBefore", cfg.stopBefore);
  rd(o, "autoTune", cfg.autoTune);
  rd(o, "pumpDriver", cfg.pumpDriver); rd(o, "retractMs", cfg.retractMs); rd(o, "retractPwm", cfg.retractPwm);
  rd(o, "cleanShots", cfg.cleanShots); rd(o, "cleanDays", cfg.cleanDays); rd(o, "cleanRinseS", cfg.cleanRinseS); rd(o, "cleanDryS", cfg.cleanDryS); rd(o, "cleanPos", cfg.cleanPos);
  rd(o, "shotsSinceClean", cfg.shotsSinceClean); rd(o, "lastClean", cfg.lastClean);
  { JsonArrayConst bm = o["beaconMap"];
    if (!bm.isNull()) { cfg.beaconN = 0; for (JsonVariantConst v : bm) { int n = v.as<int>(); if (n >= 1 && n <= MAX_LEDS && cfg.beaconN < 4) cfg.beaconMap[cfg.beaconN++] = n; } }
    const char* h = o["beaconCol"] | ""; if (h[0] == '#') cfg.beaconCol = strtoul(h + 1, nullptr, 16);
    rd(o, "beaconLvl", cfg.beaconLvl); rd(o, "beaconFx", cfg.beaconFx); }
  rd(o, "noFlowMs", cfg.noFlowMs);   rd(o, "maxPourMs", cfg.maxPourMs);
  rd(o, "settleMs", cfg.settleMs); rd(o, "dripMs", cfg.dripMs);   rd(o, "parkAfter", cfg.parkAfter);
  rd(o, "glassMin", cfg.glassMin);   rd(o, "stableMs", cfg.stableMs);
  rd(o, "angPark", cfg.angPark);
  JsonArrayConst a = o["ang"];
  if (!a.isNull()) for (int i = 0; i < NUM_PLACES && i < (int)a.size(); i++) cfg.ang[i] = a[i].as<float>();
  rd(o, "angOff", cfg.angOff);
  if (o["angMax"].is<float>()) cfg.angMax = o["angMax"].as<float>();
  else if (!a.isNull() && !o["density"].isNull()) {
    // komplette ältere Einstellungen (NVS, Standard-Einrichtung, Import) ohne Bereich:
    // so groß wählen, dass keine gespeicherte Position abgeschnitten wird
    float mx = max(120.0f, cfg.angPark);
    for (int i = 0; i < NUM_PLACES; i++) mx = max(mx, cfg.ang[i]);
    cfg.angMax = mx;
  }
  rd(o, "servoSpeed", cfg.servoSpeed);
  rd(o, "servoMinUs", cfg.servoMinUs); rd(o, "servoMaxUs", cfg.servoMaxUs);
  rd(o, "ledBright", cfg.ledBright);   rd(o, "encDiv", cfg.encDiv);
  rd(o, "ledCount", cfg.ledCount);
  rd(o, "servoReverse", cfg.servoReverse); rd(o, "servoRelease", cfg.servoRelease); rd(o, "servoHoldMs", cfg.servoHoldMs); rd(o, "servoHoldPour", cfg.servoHoldPour); rd(o, "updAuto", cfg.updAuto); rd(o, "btnBright", cfg.btnBright);
  JsonArrayConst lc = o["ledCol"], ll = o["ledLvl"], lf = o["ledFx"], bf = o["btnFx"];
  for (int i = 0; i < 7; i++) {
    if (!lc.isNull() && i < (int)lc.size()) { const char* h = lc[i] | ""; if (h[0] == '#') cfg.ledCol[i] = strtoul(h + 1, nullptr, 16); }
    if (!ll.isNull() && i < (int)ll.size()) cfg.ledLvl[i] = ll[i].as<int>();
    if (!lf.isNull() && i < (int)lf.size()) cfg.ledFx[i] = lf[i].as<int>();
    if (!bf.isNull() && i < (int)bf.size()) cfg.btnFx[i] = bf[i].as<int>();
  }
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
  clampAll();
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  settingsToJson(o);
  o.remove("hasPass"); o.remove("hasApPass");
  o["pass"] = cfg.pass;
  o["apPass"] = cfg.apPass;
  String s;
  serializeJson(d, s);
  // Als Blob speichern: NVS-Strings sind auf 4000 Bytes begrenzt, Blobs nicht
  prefs.begin("sh", false);
  size_t w = prefs.putBytes("cfgb", s.c_str(), s.length());
  // Zusätzlich als String, solange es passt – so finden ältere Versionen (Downgrade) ihre Einstellungen
  if (s.length() < 3900) prefs.putString("cfg", s); else prefs.remove("cfg");
  prefs.end();
  static size_t lastLen = 0;
  if (w != s.length()) Serial.printf("[Einstellungen] Speichern fehlgeschlagen (%u Bytes)\n", (unsigned)s.length());
  else if (s.length() / 256 != lastLen / 256) Serial.printf("[Einstellungen] %u Bytes gespeichert\n", (unsigned)s.length());
  lastLen = s.length();
}

void settingsLoad() {
  prefs.begin("sh", true);
  String s;
  size_t n = prefs.getBytesLength("cfgb");
  if (n) { std::unique_ptr<char[]> b(new char[n + 1]); prefs.getBytes("cfgb", b.get(), n); b[n] = 0; s = b.get(); }
  else s = prefs.getString("cfg", "");               // ältere Versionen
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
