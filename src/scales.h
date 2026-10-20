#pragma once
#include <Arduino.h>

// 6x HX711 mit gemeinsamem SCK – parallel und non-blocking ausgelesen.
namespace Scales {
  void     begin();
  void     loop();             // so oft wie möglich aufrufen
  uint32_t seq();              // zählt bei jedem neuen Messwert-Satz hoch
  bool     ok(int i);          // Waage liefert Daten
  float    g(int i);           // schneller Wert (Median aus 3) in Gramm
  float    gs(int i);          // geglätteter Wert in Gramm
  bool     stable(int i);      // Gewicht ruhig seit cfg.stableMs
  int32_t  raw(int i);         // geglätteter Rohwert
  float    noise(int i);       // Rauschen (Standardabweichung) in Gramm
  float    hz();               // Messrate aller Waagen (10 = RATE auf GND, 80 = RATE offen)
  float    avg(int i, int n);  // Mittelwert der letzten n schnellen Messwerte (max. 16)
  void     tare(int i);
  bool     calibrate(int i, float knownGrams);
  void     nudgeZero(int i, float frac);   // Nullpunkt-Nachführung (Drift)
}
