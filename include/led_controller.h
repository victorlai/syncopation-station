#pragma once

#include <FastLED.h>

// LED strip settings
constexpr uint8_t LED_PIN = D2;
constexpr uint16_t NUM_LEDS = 60;

// Global brightness (0–255) reference:
// 2–10   = nighttime ambient glow
// 10–25  = soft indoor evening glow
// 25–50  = visible indoors daytime
// 50–90  = bright indoor / shaded outdoor
// 90–255 = very bright / high power draw
constexpr uint8_t BRIGHTNESS = 100;

// Maximum allowed current draw
constexpr uint16_t MAX_MILLIAMPS = 500;

// Shared LED array
extern CRGB leds[NUM_LEDS];

// LED controller functions
void setupLedController();
void drawFrame(uint8_t beat, bool contact); // Handles all states and transitions in one call.
void showLeds();
void clearLeds();