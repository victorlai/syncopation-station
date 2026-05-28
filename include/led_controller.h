#pragma once

#include <FastLED.h>

// LED strip settings
constexpr uint8_t LED_PIN = D2;
// Total number of LEDs in the strip. Adjust if you use a different length.
constexpr uint16_t NUM_LEDS = 20; // 60 in 1m strip. Use less for testing

// Global brightness (0–255) reference:
// 2–10   = nighttime ambient glow
// 10–25  = soft indoor evening glow
// 25–50  = visible indoors daytime
// 50–90  = bright indoor / shaded outdoor
// 90–255 = very bright / high power draw
constexpr uint8_t BRIGHTNESS = 100;

// Maximum allowed current draw
constexpr uint16_t MAX_MILLIAMPS = 500;

// Connecting animation — thermometer fill when stable BPM is first detected.
constexpr uint32_t CONNECTING_FILL_MS     = 1500;  // ms to fill strip left→right
constexpr uint32_t CONNECTING_HOLD_MS     = 500;   // ms to hold fully lit before pulsing
constexpr uint8_t  CONNECTING_START_BEATS = 2;     // valid beats before animation begins (≤ BPM_HISTORY_SIZE)

// Shared LED array
extern CRGB leds[NUM_LEDS];

// LED controller functions
void setupLedController();
// contact    = finger detected (starts purple→black fade)
// confirmed  = held long enough (enables animation)
// connecting = enough beats gathered to start thermometer fill
// beatPulse  = decaying brightness added to red on each confirmed beat (0 = no flash)
// One write per frame — no second pass, eliminates sync artifacts.
void drawFrame(bool contact, bool confirmed, bool connecting, uint8_t bpm, uint8_t beatPulse = 0);
void showLeds();
void clearLeds();