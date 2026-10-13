#pragma once
#include <Arduino.h>

// Improv-WiFi über USB-Serial (https://www.improv-wifi.com/serial/)
// Damit fragt das Web-Flash-Tool direkt nach dem Flashen die WLAN-Daten ab.
namespace Improv {
  struct Callbacks {
    bool   (*connect)(const String& ssid, const String& pass);  // true = verbunden
    bool   (*isConnected)();
    String (*url)();
    String (*deviceName)();
  };
  void begin(const Callbacks& cb);
  void loop();
}
