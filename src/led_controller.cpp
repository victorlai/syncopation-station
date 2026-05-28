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
        if (bgLevel > 0)   bgLevel  = (bgLevel  > 2) ? bgLevel  - 2 : 0;
        else               redLevel = (redLevel < 252) ? redLevel + 3 : 255;
    } else if (contact) {
        // Contact detected but not yet confirmed — slowly dim purple over the confirmation window.
        // Rate matched to CONTACT_CONFIRM_MS so the strip reaches dark just as red begins.
        // Slow rate also means brief false-contact readings barely affect bgLevel.
        bgLevel = (bgLevel > 1) ? bgLevel - 1 : 0;
    } else {
        // No contact: fade red out first, then bring purple back.
        if (redLevel > 0)  redLevel = (redLevel > 4) ? redLevel - 4 : 0;
        else               bgLevel  = (bgLevel  < 252) ? bgLevel  + 5 : 255;
    }

    // Compute colours once per frame, not per LED.
    uint8_t purpleBrightness = scale8(beatsin8(6, 5, 80), bgLevel);
    uint8_t pulseBPM         = (bpm > 0) ? bpm : 60;
    uint8_t redBrightness    = scale8(beatsin8(pulseBPM, 0, 220), redLevel);

    // Single-sensor mode: full strip follows Person A.
    // When Person B is wired, restore: if (confirmed && i >= NUM_LEDS / 2) leds[i] = CRGB::Black;
    for (uint16_t i = 0; i < NUM_LEDS; ++i) {
        if (redLevel > 0) {
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