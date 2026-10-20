#include "online_update.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include "version.h"
#include "ca_roots.h"

#define UPD_ASSET "liquid-harvest-ota.bin"

namespace OnlineUpd {

struct Rel { String tag; String date; bool pre; uint32_t size; };

static SemaphoreHandle_t mtx = nullptr;
static std::vector<Rel> rels;
static String latest, err, installTag, doneMsg;
static volatile uint8_t st = 0;        // 0 bereit, 1 prüft, 2 lädt
static volatile int8_t  prog = -1;
static volatile bool    isOnline = false, listChanged = false, doneFlag = false, doneOk = false;
static uint32_t lastCheck = 0, lastAuto = 0;
static bool everChecked = false;

static void lock()   { if (!mtx) mtx = xSemaphoreCreateMutex(); xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx); }

int cmpVersion(const char* a, const char* b) {
  if (*a == 'v' || *a == 'V') a++;
  if (*b == 'v' || *b == 'V') b++;
  for (int i = 0; i < 3; i++) {
    long x = strtol(a, (char**)&a, 10), y = strtol(b, (char**)&b, 10);
    if (x != y) return x < y ? -1 : 1;
    if (*a == '.') a++;
    if (*b == '.') b++;
  }
  return 0;
}

// Liest einen HTTP-Body mit "Transfer-Encoding: chunked" als normalen Datenstrom (für ArduinoJson)
class ChunkedStream : public Stream {
  Stream& s; long left = 0; bool done = false;
  bool next() {
    if (done) return false;
    String line = s.readStringUntil('\n'); line.trim();
    if (!line.length()) { line = s.readStringUntil('\n'); line.trim(); }
    left = strtol(line.c_str(), nullptr, 16);
    if (left <= 0) { done = true; left = 0; return false; }
    return true;
  }
 public:
  explicit ChunkedStream(Stream& src) : s(src) {}
  int available() override { if (!left && !next()) return 0; int a = s.available(); return a < left ? a : (int)left; }
  int read() override {
    if (!left && !next()) return -1;
    uint8_t c;
    if (s.readBytes(&c, 1) != 1) return -1;
    if (--left == 0) { uint8_t crlf[2]; s.readBytes(crlf, 2); }   // CRLF nach jedem Block
    return c;
  }
  int peek() override { if (!left && !next()) return -1; return s.peek(); }
  size_t write(uint8_t) override { return 0; }
};

static bool timeValid() { return time(nullptr) > 1700000000; }

// ---------------------------------------------------------------- Releases abfragen
static void doCheck() {
  String e;
  std::vector<Rel> v;
  if (!timeValid()) e = "Uhrzeit noch nicht gestellt (NTP) – gleich nochmal versuchen";
  else {
    WiFiClientSecure cl;
    cl.setCACert(CA_ROOTS);
    cl.setTimeout(15);
    HTTPClient h;
    h.setReuse(false);
    h.setTimeout(15000);
    h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    h.setUserAgent("LiquidHarvest/" FW_VERSION);
    if (!h.begin(cl, "https://api.github.com/repos/" FW_REPO "/releases?per_page=15")) e = "Verbindung fehlgeschlagen";
    else {
      h.addHeader("Accept", "application/vnd.github+json");
      int code = h.GET();
      if (code == 200) {
        JsonDocument filter;
        JsonObject f = filter[0].to<JsonObject>();
        f["tag_name"] = true; f["published_at"] = true; f["prerelease"] = true; f["draft"] = true;
        f["assets"][0]["name"] = true; f["assets"][0]["size"] = true;
        JsonDocument d;
        // GitHub antwortet meist "chunked" (getSize() < 0) – dann über den Decoder lesen
        WiFiClient& raw = h.getStream();
        ChunkedStream ch(raw);
        DeserializationError de = h.getSize() < 0
          ? deserializeJson(d, ch, DeserializationOption::Filter(filter))
          : deserializeJson(d, raw, DeserializationOption::Filter(filter));
        if (de) e = String("Antwort von GitHub unlesbar (") + de.c_str() + ")";
        else for (JsonObject r : d.as<JsonArray>()) {
          if (r["draft"] | false) continue;
          uint32_t sz = 0;
          for (JsonObject a : r["assets"].as<JsonArray>()) if (strcmp(a["name"] | "", UPD_ASSET) == 0) sz = a["size"] | 0;
          if (!sz) continue;                   // Release ohne Update-Datei
          String date = r["published_at"] | "";
          v.push_back({String(r["tag_name"] | ""), date.substring(0, 10), (bool)(r["prerelease"] | false), sz});
        }
      } else if (code < 0) e = "Keine Internetverbindung (" + h.errorToString(code) + ")";
      else if (code == 403 || code == 429) e = "GitHub: zu viele Anfragen – in einer Stunde nochmal";
      else e = "GitHub antwortet mit Fehler " + String(code);
      h.end();
    }
  }
  std::sort(v.begin(), v.end(), [](const Rel& a, const Rel& b) { return cmpVersion(a.tag.c_str(), b.tag.c_str()) > 0; });
  lock();
  if (e.length()) { err = e; isOnline = false; }
  else {
    err = ""; isOnline = true; rels = v; latest = "";
    for (auto& r : rels) if (!r.pre) { latest = r.tag; break; }
    listChanged = true;
  }
  lastCheck = millis(); everChecked = true;
  unlock();
  Serial.printf("[Update] Prüfung: %s, %u Versionen, neueste %s\n", e.length() ? e.c_str() : "ok", (unsigned)v.size(), latest.c_str());
}

// ---------------------------------------------------------------- Herunterladen + flashen
static void doInstall() {
  String tag; lock(); tag = installTag; unlock();
  WiFiClientSecure cl;
  cl.setCACert(CA_ROOTS);
  cl.setTimeout(20);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // github.com -> release-assets.githubusercontent.com
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress([](int cur, int tot) { prog = tot > 0 ? (int8_t)((int64_t)cur * 100 / tot) : 0; });
  String url = "https://github.com/" FW_REPO "/releases/download/" + tag + "/" UPD_ASSET;
  Serial.printf("[Update] Lade %s\n", url.c_str());
  t_httpUpdate_return r = httpUpdate.update(cl, url);
  lock();
  doneOk = r == HTTP_UPDATE_OK;
  doneMsg = doneOk ? "Version " + tag + " installiert – Neustart" : "Update fehlgeschlagen: " + httpUpdate.getLastErrorString();
  doneFlag = true;
  unlock();
  Serial.printf("[Update] %s\n", doneMsg.c_str());
}

static void worker(void* arg) {
  if ((intptr_t)arg == 2) doInstall(); else doCheck();
  prog = -1;
  st = 0;
  vTaskDelete(nullptr);
}

static bool start(uint8_t what) {
  if (st) return false;
  st = what;
  prog = what == 2 ? 0 : -1;
  if (xTaskCreatePinnedToCore(worker, "upd", 12288, (void*)(intptr_t)what, 1, nullptr, 1) != pdPASS) { st = 0; return false; }
  return true;
}

// ---------------------------------------------------------------- API
void loop(bool staConnected, bool autoCheck) {
  static uint32_t connectedSince = 0;
  if (!staConnected) { connectedSince = 0; if (isOnline && !st) isOnline = false; return; }
  uint32_t now = millis();
  if (!connectedSince) connectedSince = now;
  if (!autoCheck || st) return;
  // automatisch: 30 s nach dem Verbinden (NTP ist dann gestellt), danach alle 12 h; nach Fehler in 10 min nochmal
  bool due = !everChecked ? now - connectedSince > 30000
           : now - lastCheck > (err.length() ? 600000UL : 12UL * 3600 * 1000);
  if (due && now - lastAuto > 60000) { lastAuto = now; start(1); }
}

bool requestCheck() {
  if (st) return false;
  if (everChecked && millis() - lastCheck < 10000) return false;
  return start(1);
}

bool install(const String& tag, String& why) {
  if (st) { why = "Es läuft schon eine Prüfung oder ein Download"; return false; }
  if (!isOnline) { why = "Keine Verbindung zu GitHub – erst „Jetzt prüfen“"; return false; }
  bool known = false;
  lock(); for (auto& r : rels) if (r.tag == tag) known = true; installTag = tag; unlock();
  if (!known) { why = "Version " + tag + " gibt es nicht"; return false; }
  if (!start(2)) { why = "Download konnte nicht starten"; return false; }
  return true;
}

bool busy() { return st != 0; }
bool downloading() { return st == 2; }
bool online() { return isOnline; }
bool takeListChanged() { if (!listChanged) return false; listChanged = false; return true; }

bool takeDone(bool& ok, String& msg) {
  if (!doneFlag) return false;
  lock(); ok = doneOk; msg = doneMsg; doneFlag = false; unlock();
  return true;
}

void statusJson(JsonObject o) {
  o["st"] = st;
  if (prog >= 0) o["p"] = prog;
  o["on"] = isOnline;
  lock();
  if (latest.length()) { o["new"] = latest; o["av"] = cmpVersion(latest.c_str(), FW_VERSION) > 0; }
  if (err.length()) o["err"] = err;
  if (everChecked) o["ago"] = (millis() - lastCheck) / 60000;   // Minuten seit der letzten Prüfung
  unlock();
}

void listJson(JsonArray a) {
  lock();
  for (auto& r : rels) {
    JsonObject x = a.add<JsonObject>();
    x["tag"] = r.tag; x["d"] = r.date; x["kb"] = (r.size + 512) / 1024;
    if (r.pre) x["pre"] = true;
  }
  unlock();
}

}  // namespace OnlineUpd
