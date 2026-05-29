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

// Maximum current draw — scales with strip length so FastLED doesn't silently dim a longer strip.
// Formula: ~25 mA per LED (red channel at BRIGHTNESS ≈ 100 draws well under this).
constexpr uint16_t MAX_MILLIAMPS = NUM_LEDS * 25;

// Connecting animation constants.
constexpr uint32_t CONNECTING_FILL_MS   = 2500;  // ms to fill strip from beat-count LEDs to full
constexpr uint32_t CONNECTING_HOLD_MS   = 500;   // ms to hold fully lit before pulsing
constexpr uint32_t DRAIN_MS             = 800;   // ms for right-to-left wipe when contact is lost
// Starting LED count when entering GATHER — ~1/6 of strip so there's something to see immediately.
constexpr uint8_t  GATHER_LEDS_PER_BEAT = NUM_LEDS / 6;
// Must match BPM_HISTORY_SIZE in heartbeat.cpp. Used to map beat fraction → strip fraction.
constexpr uint8_t  GATHER_BEAT_TOTAL    = 4;

// Shared LED array
extern CRGB leds[NUM_LEDS];

// LED controller functions
void setupLedController();
// possibleContact = raw near-zero detected (earliest touch hint, before contact quality check)
// contact         = sustained signal quality (getContactGood)
// confirmed       = held long enough to trust (isContactConfirmed)
// beatCount       = valid beats gathered; drives fill level (0 → BPM_HISTORY_SIZE)
// bpm             = stable BPM; non-zero when beatCount == BPM_HISTORY_SIZE
// beatPulse       = decaying brightness on each confirmed beat
void drawFrame(bool possibleContact, bool contact, bool confirmed,
               int beatCount, uint8_t bpm, uint8_t beatPulse = 0);
void showLeds();
void clearLeds();