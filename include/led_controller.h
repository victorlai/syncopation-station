#pragma once

#include <FastLED.h>

// LED strip settings
constexpr uint8_t LED_PIN = D2;
// Total number of LEDs in the strip. Adjust if you use a different length.
constexpr uint16_t NUM_LEDS = 60;

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
constexpr uint32_t CONNECTING_FILL_MS = 2500;  // ms to fill half-strip from initial LEDs to full
constexpr uint32_t CONNECTING_HOLD_MS = 500;   // ms to hold fully lit before pulsing
constexpr uint32_t DRAIN_MS           = 800;   // ms for edge-to-centre wipe when contact is lost

// Shared LED array
extern CRGB leds[NUM_LEDS];

// LED controller functions
void setupLedController();
// Each sensor drives one half of the strip independently.
// Sensor A (D3) → leds[0..NUM_LEDS/2-1]    (left half, fills left→right)
// Sensor B (D8) → leds[NUM_LEDS-1..NUM_LEDS/2] (right half, fills right→centre)
void drawFrame(
    bool possibleContactA, bool contactA, bool confirmedA, int beatCountA, uint8_t bpmA, uint8_t beatPulseA,
    bool possibleContactB, bool contactB, bool confirmedB, int beatCountB, uint8_t bpmB, uint8_t beatPulseB);
void showLeds();
void clearLeds();

// Trigger the 15-second synchronized animation sequence.
// For testing: press 'S' in the serial monitor.
// Production: call when both sensors confirm sync.
void triggerSyncAnimation();
void cancelSyncAnimation();  // typing 's' again cancels and returns to IDLE
bool isSyncAnimActive();