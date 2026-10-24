#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "pins.h"

// Getränke-Profil: alles, was vom Getränk abhängt (Menge, Dichte, Pumpe, Nachlauf)
constexpr int MAX_PROF = 6;
struct DrinkProfile {
  String   name = "Schnaps";
  float    shotMl = 20.0f, doubleMl = 40.0f, density = 0.95f;
  uint8_t  pwmFast = 100, pwmSlow = 35;
  float    slowBefore = 3.0f, stopBefore = 0.3f;
  uint16_t dripMs = 800;
};

// Alle Einstellungen – werden als JSON im NVS (Preferences) gespeichert
// und können komplett über die Weboberfläche geändert werden.
struct Settings {
  // --- Dosierung ---
  // Die Dosier-Felder unten sind immer die Werte des aktiven Getränks (prof[profIdx])
  uint8_t  profN = 0, profIdx = 0;
  DrinkProfile prof[MAX_PROF];
  float    shotMl      = 20.0f;   // Taster Einfach-Klick
  float    doubleMl    = 40.0f;   // Taster Doppel-Klick
  float    density     = 0.95f;   // g/ml (40 % Schnaps ≈ 0,95)
  uint8_t  pwmFast     = 100;     // Pumpenleistung Hauptphase [%]
  uint8_t  pwmSlow     = 35;      // Pumpenleistung Feindosierung [%]
  float    slowBefore  = 3.0f;    // so viele g vor Ziel auf langsam schalten
  float    stopBefore  = 0.3f;    // so viele g vor Ziel stoppen (Nachlauf)
  bool     autoTune    = true;    // Nachlauf nach jedem Glas selbst nachstellen
  bool     pumpInv = false;       // Pumpendrehrichtung umkehren (nur H-Brücke)
  uint8_t  pumpDriver  = 0;       // 0 = MOSFET-Modul (D4184), 1 = H-Brücke (DRV8871, BTS7960, L298N …)
  uint16_t retractMs   = 300;     // H-Brücke: nach dem Einschenken so lange rückwärts (gegen Nachtropfen), 0 = aus
  uint8_t  retractPwm  = 80;      // Leistung beim Zurücksaugen [%]

  // --- Reinigung ---
  uint16_t cleanShots  = 50;      // Erinnerung nach so vielen Gläsern (0 = aus)
  uint8_t  cleanDays   = 7;       // … oder nach so vielen Tagen (0 = aus, braucht Uhrzeit aus dem Heimnetz)
  uint16_t cleanRinseS = 20;      // Spülen mit Wasser [s]
  uint16_t cleanDryS   = 10;      // Leerpumpen [s]
  uint8_t  cleanPos    = 0;       // Reinigungsposition: 0 = Park, 1..6 = Glas
  uint16_t shotsSinceClean = 0;
  uint32_t lastClean   = 0;       // Unix-Zeit der letzten Reinigung (0 = unbekannt)
  uint32_t noFlowMs    = 3000;    // Abbruch, wenn Gewicht so lange nicht steigt
  uint32_t maxPourMs   = 30000;   // absolute Höchstdauer pro Glas
  uint32_t settleMs    = 400;     // Wartezeit nach Servo-Bewegung
  uint16_t dripMs      = 800;     // Pause nach dem Einfüllen, bevor das Rohr schwenkt (Nachtropfen)
  bool     parkAfter   = true;    // nach Web-Befehl zurück in Parkposition

  // --- Glaserkennung ---
  float    glassMin    = 1.5f;    // ab diesem Gewicht gilt "Glas da" (Plastikstamperl!)
  uint32_t stableMs    = 500;     // so lange muss das Gewicht ruhig sein

  // --- Servo ---
  float    angPark     = 0.0f;
  float    ang[NUM_PLACES] = {18.3f, 36.7f, 55.0f, 73.3f, 91.7f, 110.0f};  // Park 0°, ausgefahren 110°
  float    servoSpeed  = 120.0f;  // °/s
  float    servoAccel  = 360.0f;  // °/s² – sanftes Anfahren/Bremsen (0 = aus)
  // Antrieb: 0 = Servo, 1 = Schrittmotor (STEP/DIR/EN + Endschalter)
  uint8_t  drive       = 0;
  uint16_t stepRev     = 200;     // Vollschritte pro Motorumdrehung (1,8° = 200)
  uint8_t  stepMicro   = 16;      // Microstepping am Treiber
  float    stepGear    = 1.0f;    // Untersetzung Motor : Rohr (z. B. 3 bei 20→60 Zähnen)
  bool     stepDirInv  = false;   // Drehrichtung des Motors umkehren
  bool     stepRelease = false;   // nach Erreichen stromlos (Achtung: Position kann verloren gehen)
  bool     homeInv     = false;   // Endschalter schaltet HIGH statt LOW
  float    homeSpeed   = 20.0f;   // °/s bei der Referenzfahrt
  bool     servoReverse = false;  // Drehrichtung umkehren (0° liegt dann auf der anderen Seite)
  bool     servoRelease = true;   // Servo nach Erreichen der Position stromlos schalten (kein Brummen/Zittern)
  uint16_t servoHoldMs  = 700;    // so lange nach dem Ankommen noch halten
  bool     servoHoldPour = true;
  bool     updAuto      = true;
  // Party-Modus: Glas hinstellen -> wird automatisch gefüllt
  bool     party        = false;
  uint16_t partyDelayMs = 2000;   // so lange muss das leere Glas ruhig stehen
  float    partyMaxG    = 120;    // schwerere Dinge (Handy, volles Glas …) werden nicht befüllt   // automatisch nach neuen Versionen suchen (GitHub)  // beim Einschenken angesteuert lassen (Stoß dreht Rohr nicht weg)
  // Schwenkbereich (eingelernt): angOff = Servo-Winkel, der in der App 0° ist; angMax = größter App-Winkel.
  // Alle Positionen sind relativ zu angOff; das Rohr fährt nie außerhalb 0…angMax.
  float    angOff      = 0;
  float    angMax      = 120;
  uint16_t servoMinUs  = 500;     // Impuls bei 0°
  uint16_t servoMaxUs  = 2500;    // Impuls bei 180° (JX 5521MG: 500–2500 µs)

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
  // Rundumleuchte: zusätzliche WS2812B in der Kette, blinkt solange der Drescher arbeitet
  uint8_t  beaconN = 0;
  uint8_t  beaconMap[4] = {0, 0, 0, 0};         // LED-Nummern (ab 1)
  uint32_t beaconCol = 0xFF8C00;
  uint8_t  beaconLvl = 100;
  uint16_t beaconSpeed = 100;
  // Status an der Rundumleuchte im Stillstand (die Fahr-/Pump-Animation hat immer Vorrang)
  bool     staInd    = true;      // Heimnetz verbunden: schwach grün
  uint32_t staIndCol = 0x00FF40;
  uint8_t  staIndLvl = 6;         // Helligkeit in %
  bool     apInd     = true;      // Hotspot aktiv: schwach blau blitzen
  uint32_t apIndCol  = 0x0040FF;
  uint8_t  apIndLvl  = 15;   // Tempo der Muster in % (25 = langsam, 300 = schnell)
  uint8_t  beaconFx  = 0;    // 0 Drehlicht, 1 abwechselnd faden, 2 Doppelblitz, 3 Wechselblitz, 4 Lauflicht, 5 Dauerlicht
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
void profileSelect(int i);                    // aktives Getränk wechseln (speichert)
bool profileAdd(const String& name);
bool profileRename(int i, const String& name);
bool profileDelete(int i);
void settingsSave();
void settingsFactoryReset();
void settingsToJson(JsonObject o);            // ohne Passwörter
void settingsFromJson(JsonObjectConst o);     // ändert nur vorhandene Felder
