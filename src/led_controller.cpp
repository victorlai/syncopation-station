#include "led_controller.h"

// LED data array
CRGB leds[NUM_LEDS];

void setupLedController() {

    // Initialize LED strip
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);

    // Limit total power draw
    FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);

    // Global brightness (0–255)
    FastLED.setBrightness(BRIGHTNESS);

    // Start with LEDs off
    FastLED.clear();
    FastLED.show();
}

// Two-phase transition: purple fades to black first, then red fades in. Reverses on release.
// This ensures we never jump directly between purple and red.
static uint8_t bgLevel  = 255;  // 255 = full purple, 0 = black
static uint8_t redLevel = 0;    // 0 = black, 255 = full red

void drawFrame(bool contact, bool confirmed, uint8_t bpm) {
    if (confirmed) {
        // Confirmed: finish fading purple out, then fade red in.
        if (bgLevel > 0)   bgLevel  = (bgLevel  > 3) ? bgLevel  - 3 : 0;
        else               redLevel = (redLevel < 251) ? redLevel + 4 : 255;
    } else if (contact) {
        // Contact detected but not yet confirmed — fade purple to black quickly
        // so the user sees the strip respond, but no red shows until confirmed.
        bgLevel = (bgLevel > 8) ? bgLevel - 8 : 0;
    } else {
        // No contact: fade red out first, then bring purple back.
        if (redLevel > 0)  redLevel = (redLevel > 3) ? redLevel - 3 : 0;
        else               bgLevel  = (bgLevel  < 253) ? bgLevel  + 2 : 255;
    }

    // Compute colours once per frame, not per LED.
    uint8_t purpleBrightness = scale8(beatsin8(6, 60, 160), bgLevel);
    uint8_t pulseBPM         = (bpm > 0) ? bpm : 60;
    uint8_t redBrightness    = scale8(beatsin8(pulseBPM, 0, 220), redLevel);

    for (uint16_t i = 0; i < NUM_LEDS; ++i) {
        if (confirmed && i >= NUM_LEDS / 2) {
            leds[i] = CRGB::Black;  // Second half reserved for Person B.
        } else if (redLevel > 0) {
            leds[i] = CRGB(redBrightness, 0, 0);
        } else {
            leds[i] = CHSV(200, 180, purpleBrightness);
        }
    }
}

// Send data to LEDs
void showLeds() {
    FastLED.show();
}

// Clear LED buffer
void clearLeds() {
    FastLED.clear();
}