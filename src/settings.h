#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "pins.h"

// Alle Einstellungen – werden als JSON im NVS (Preferences) gespeichert
// und können komplett über die Weboberfläche geändert werden.
struct Settings {
  // --- Dosierung ---
  float    shotMl      = 20.0f;   // Taster Einfach-Klick
  float    doubleMl    = 40.0f;   // Taster Doppel-Klick
  float    density     = 0.95f;   // g/ml (40 % Schnaps ≈ 0,95)
  uint8_t  pwmFast     = 100;     // Pumpenleistung Hauptphase [%]
  uint8_t  pwmSlow     = 35;      // Pumpenleistung Feindosierung [%]
  float    slowBefore  = 3.0f;    // so viele g vor Ziel auf langsam schalten
  float    stopBefore  = 0.3f;    // so viele g vor Ziel stoppen (Nachlauf)
  uint32_t noFlowMs    = 3000;    // Abbruch, wenn Gewicht so lange nicht steigt
  uint32_t maxPourMs   = 30000;   // absolute Höchstdauer pro Glas
  uint32_t settleMs    = 400;     // Wartezeit nach Servo-Bewegung
  bool     parkAfter   = true;    // nach Web-Befehl zurück in Parkposition

  // --- Glaserkennung ---
  float    glassMin    = 1.5f;    // ab diesem Gewicht gilt "Glas da" (Plastikstamperl!)
  uint32_t stableMs    = 500;     // so lange muss das Gewicht ruhig sein

  // --- Servo ---
  float    angPark     = 0.0f;
  float    ang[NUM_PLACES] = {18.3f, 36.7f, 55.0f, 73.3f, 91.7f, 110.0f};  // Park 0°, ausgefahren 110°
  float    servoSpeed  = 120.0f;  // °/s
  bool     servoRelease = true;   // Servo nach Erreichen der Position stromlos schalten (kein Brummen/Zittern)
  uint16_t servoHoldMs  = 700;    // so lange nach dem Ankommen noch halten
  uint16_t servoMinUs  = 500;     // Impuls bei 0°
  uint16_t servoMaxUs  = 2400;    // Impuls bei 180°

  // --- LEDs / Bedienung ---
  uint8_t  ledBright   = 60;      // %
  uint8_t  ledCount    = 6;       // LEDs in der Kette
  // Glas-LEDs je Zustand: 0 kein Glas, 1 bereit, 2 Rohr zielt, 3 schenkt ein, 4 fertig, 5 Fehler, 6 Waage defekt
  uint32_t ledCol[7] = {0xFF0000, 0x003CFF, 0xFF9600, 0xFF6E00, 0x00FF28, 0xFF0000, 0x780078};
  uint8_t  ledLvl[7] = {10, 100, 100, 100, 100, 100, 30};   // Helligkeit %
  uint8_t  ledFx[7]  = {0, 0, 1, 2, 0, 3, 3};               // 0 an, 1 pulsieren, 2 schnell pulsieren, 3 blinken
  // Taster-LED je Zustand: 0 bereit (Auto-Fill), 1 keine Gläser, 2 Rohr über Glas, 3 Rohr über leerem Platz,
  //                        4 schenkt ein, 5 Fehler, 6 alle Gläser gefüllt
  uint8_t  btnFx[7]  = {1, 3, 1, 2, 4, 5, 6};   // 0 aus, 1 an, 2 gedimmt, 3 atmen, 4 pulsieren, 5 blinken, 6 Doppelblitz
  uint8_t  btnBright = 100;                     // %
  uint8_t  ledMapN[NUM_PLACES] = {1, 1, 1, 1, 1, 1};                       // Anzahl LEDs je Glas
  uint8_t  ledMap[NUM_PLACES][MAX_LEDS_PER_GLASS] = {{1}, {2}, {3}, {4}, {5}, {6}};  // LED-Nummern (ab 1)
  uint8_t  encDiv      = 4;       // Flanken pro Raste (KY-040: meist 4, manche 2)
  bool     encReverse  = false;

  // --- Eigenes Mähdrescher-Bild (liegt im LittleFS) ---
  bool     imgOn       = false;
  float    imgW        = 380;     // Breite in Szenen-Einheiten
  float    imgX        = -240;    // linke obere Ecke
  float    imgY        = -215;
  uint32_t imgVer      = 0;       // ändert sich bei jedem Upload (Cache)

  // --- Darstellung Abtankrohr in der App ---
  String   augCol      = "#2f8f4e";
  float    augW        = 11;      // Dicke
  float    augPX       = 0;       // Ansatzpunkt (Szenen-Koordinaten)
  float    augPY       = 0;
  float    augPZ       = 134;     // Höhe am Ansatz
  float    augEZ       = 134;     // Höhe am Rohrende
  bool     augRiser    = true;    // Drehturm zeichnen

  // --- weitere Darstellungs-Einstellungen der Web-App (JSON, von der App verwaltet) ---
  String   ui          = "{}";

  // --- Waagen ---
  int32_t  offset[NUM_PLACES] = {0, 0, 0, 0, 0, 0};
  float    factor[NUM_PLACES] = {420, 420, 420, 420, 420, 420};  // Rohwert pro Gramm
  uint8_t  enabledMask = 0x3F;    // Bit i = Waage i+1 vorhanden

  // --- WLAN ---
  String   ssid;
  String   pass;
  String   host        = "liquidharvest";
  String   apPass      = "prost1234";  // leer = offener Hotspot
  bool     apAlways    = false;        // Hotspot zusätzlich immer an (Party-Modus)
};

extern Settings cfg;

void settingsLoad();
void settingsSave();
void settingsFactoryReset();
void settingsToJson(JsonObject o);            // ohne Passwörter
void settingsFromJson(JsonObjectConst o);     // ändert nur vorhandene Felder
