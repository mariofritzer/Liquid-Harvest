#include "improv_serial.h"
#include "version.h"
#include <WiFi.h>

namespace Improv {

enum : uint8_t { T_STATE = 0x01, T_ERROR = 0x02, T_RPC = 0x03, T_RPC_RESULT = 0x04 };
enum : uint8_t { S_READY = 0x02, S_PROVISIONING = 0x03, S_PROVISIONED = 0x04 };
enum : uint8_t { E_NONE = 0x00, E_INVALID_RPC = 0x01, E_UNKNOWN_RPC = 0x02, E_UNABLE_TO_CONNECT = 0x03 };
enum : uint8_t { C_WIFI = 0x01, C_STATE = 0x02, C_INFO = 0x03, C_SCAN = 0x04 };

static Callbacks cb;
static uint8_t buf[256];
static size_t len = 0;

static void sendPacket(uint8_t type, const uint8_t* data, uint8_t n) {
  uint8_t hdr[9] = {'I', 'M', 'P', 'R', 'O', 'V', 0x01, type, n};
  uint8_t sum = 0;
  for (uint8_t b : hdr) sum += b;
  for (int i = 0; i < n; i++) sum += data[i];
  Serial.write(hdr, 9);
  if (n) Serial.write(data, n);
  Serial.write(sum);
  Serial.write('\n');
  Serial.flush();
}

static void sendState(uint8_t s) { sendPacket(T_STATE, &s, 1); }
static void sendError(uint8_t e) { sendPacket(T_ERROR, &e, 1); }

static void sendResult(uint8_t cmd, std::initializer_list<String> strs) {
  uint8_t d[250];
  size_t p = 2;
  for (const String& s : strs) {
    size_t l = min((size_t)s.length(), (size_t)(sizeof(d) - p - 1));
    d[p++] = (uint8_t)l;
    memcpy(d + p, s.c_str(), l);
    p += l;
  }
  d[0] = cmd;
  d[1] = (uint8_t)(p - 2);
  sendPacket(T_RPC_RESULT, d, (uint8_t)p);
}

static void handleRpc(const uint8_t* d, uint8_t n) {
  if (n < 2 || d[1] > n - 2) { sendError(E_INVALID_RPC); return; }
  uint8_t cmd = d[0];
  const uint8_t* p = d + 2;
  uint8_t pl = d[1];

  switch (cmd) {
    case C_WIFI: {
      if (pl < 1) { sendError(E_INVALID_RPC); return; }
      uint8_t sl = p[0];
      if (1 + sl + 1 > pl) { sendError(E_INVALID_RPC); return; }
      String ssid((const char*)p + 1, sl);
      uint8_t wl = p[1 + sl];
      if (2 + sl + wl > pl) { sendError(E_INVALID_RPC); return; }
      String pass((const char*)p + 2 + sl, wl);
      sendError(E_NONE);
      sendState(S_PROVISIONING);
      if (cb.connect(ssid, pass)) {
        sendState(S_PROVISIONED);
        sendResult(C_WIFI, {cb.url()});
      } else {
        sendError(E_UNABLE_TO_CONNECT);
        sendState(S_READY);
      }
      break;
    }
    case C_STATE:
      if (cb.isConnected()) {
        sendState(S_PROVISIONED);
        sendResult(C_STATE, {cb.url()});
      } else {
        sendState(S_READY);
      }
      break;
    case C_INFO:
      sendResult(C_INFO, {FW_NAME, FW_VERSION, "ESP32", cb.deviceName()});
      break;
    case C_SCAN: {
      int n = WiFi.scanNetworks(false, false);
      for (int i = 0; i < n && i < 20; i++) {
        sendResult(C_SCAN, {WiFi.SSID(i), String(WiFi.RSSI(i)),
                            WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"});
      }
      WiFi.scanDelete();
      sendResult(C_SCAN, {});
      break;
    }
    default:
      sendError(E_UNKNOWN_RPC);
  }
}

// Liefert false, wenn das Byte nicht zu einem gültigen Paket passt
static bool parseByte(uint8_t b) {
  static const char hdr[] = "IMPROV";
  size_t at = len;
  buf[len++] = b;
  if (at < 6) return b == (uint8_t)hdr[at];
  if (at == 6) return b == 0x01;
  if (at <= 8) return true;                    // Typ, Länge
  uint8_t dlen = buf[8];
  if (at < 9 + (size_t)dlen) return true;
  // Prüfsumme
  uint8_t sum = 0;
  for (size_t i = 0; i < at; i++) sum += buf[i];
  if (sum == b && buf[7] == T_RPC) handleRpc(buf + 9, dlen);
  len = 0;
  return true;
}

void begin(const Callbacks& c) { cb = c; }

void loop() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    if (len >= sizeof(buf)) len = 0;
    if (!parseByte(b)) {
      len = 0;
      if (b == 'I') buf[len++] = b;
    }
  }
}

}  // namespace Improv
