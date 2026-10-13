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
  float    ang[NUM_PLACES] = {15, 45, 75, 105, 135, 165};
  float    servoSpeed  = 120.0f;  // °/s
  uint16_t servoMinUs  = 500;     // Impuls bei 0°
  uint16_t servoMaxUs  = 2400;    // Impuls bei 180°

  // --- LEDs / Bedienung ---
  uint8_t  ledBright   = 60;      // %
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
