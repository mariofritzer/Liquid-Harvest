#pragma once
// =====================================================================
//  Liquid Harvest – Pinbelegung (ESP32 DevKit / WROOM-32)
// =====================================================================
//  Grundgedanke:
//  * Alle 6 HX711-DOUT-Leitungen liegen auf GPIO 32-39. Diese Pins stehen
//    im selben Eingangsregister (GPIO_IN1_REG) -> alle 6 Waagen werden bei
//    jedem Takt mit EINEM Registerzugriff gleichzeitig gelesen.
//    GPIO 34/35/36/39 sind "input only" – perfekt für reine Datenleitungen.
//  * Keine Strapping-Pins (0, 2, 5, 12, 15) belegt.
//  * Keine Flash-Pins (6-11) belegt.
//  * Frei für Erweiterungen: GPIO 19, 21, 22 (I2C z. B. für ein OLED),
//    GPIO 14, 5, 2, 15, 12 (mit Vorsicht).
// =====================================================================

#include <stdint.h>

constexpr int NUM_PLACES = 6;
constexpr int MAX_LEDS = 64;            // max. LEDs in der WS2812B-Kette
constexpr int MAX_LEDS_PER_GLASS = 12;  // max. LEDs, die einem Glas zugeordnet werden können

// Waagen (HX711): gemeinsamer Takt, je eine Datenleitung
constexpr int PIN_HX_SCK = 4;
constexpr int PIN_HX_DT[NUM_PLACES] = {36, 39, 34, 35, 32, 33};  // Glas 1..6

// Aktoren
constexpr int PIN_SERVO = 13;     // MG90S Signal
constexpr int PIN_PUMP  = 27;     // D4184 Gate (PWM)
constexpr int PIN_LEDS  = 23;     // WS2812B Data-In (LED 1 = Glas 1)

// Bedienung
constexpr int PIN_BTN     = 25;   // Haupt-Taster, schaltet gegen GND
constexpr int PIN_BTN_LED = 26;   // Taster-Beleuchtung (PWM)
constexpr int PIN_ENC_CLK = 16;   // KY-040 CLK (A)
constexpr int PIN_ENC_DT  = 17;   // KY-040 DT  (B)
constexpr int PIN_ENC_SW  = 18;   // KY-040 SW (Taster gegen GND)

// LEDC-Kanäle (Kanal n nutzt Timer n/2)
constexpr int CH_SERVO   = 0;     // Timer 0, 50 Hz
constexpr int CH_PUMP    = 2;     // Timer 1, 1 kHz
constexpr int CH_BTN_LED = 4;     // Timer 2, 5 kHz
