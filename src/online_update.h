#pragma once
// Online-Update: Releases von GitHub abfragen und eine gewählte Version direkt herunterladen + flashen.
// Netzwerkzugriffe laufen in einer eigenen Task, damit Waagen, LEDs und Weboberfläche weiterlaufen.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace OnlineUpd {
void loop(bool staConnected, bool autoCheck);   // im Hauptloop aufrufen
bool requestCheck();                             // false = läuft schon / zu früh
bool install(const String& tag, String& why);    // false + why = abgelehnt
bool busy();                                     // Prüfung oder Download läuft
bool downloading();
bool online();                                   // letzte Prüfung erfolgreich (Internet + GitHub erreichbar)
bool takeListChanged();                          // true einmalig nach neuer Release-Liste
bool takeDone(bool& ok, String& msg);            // true einmalig nach Ende eines Downloads
void statusJson(JsonObject o);                   // kompakter Status für jede Statusmeldung
void listJson(JsonArray a);                      // Release-Liste
int  cmpVersion(const char* a, const char* b);   // <0, 0, >0
}
