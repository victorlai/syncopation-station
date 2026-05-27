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

// bgLevel tracks the purple standby brightness: 255 = full purple, 0 = black.
// It fades toward 0 when contact is detected, and back to 255 when released.
static uint8_t bgLevel = 255;

void drawFrame(uint8_t beat, bool contact) {
    // Fade purple out when finger is on sensor, fade back in when released.
    // Different speeds: faster to black (deliberate sensing feel), slower back to purple (relaxing release).
    if (contact) {
        bgLevel = bgLevel > 4 ? bgLevel - 4 : 0;
    } else {
        bgLevel = bgLevel < 253 ? bgLevel + 2 : 255;
    }

    // Draw purple standby background, scaled by bgLevel (disappears on contact).
    uint8_t purpleBrightness = scale8(beatsin8(6, 60, 160), bgLevel);
    CRGB bg = CHSV(200, 180, purpleBrightness);
    for (uint16_t i = 0; i < NUM_LEDS; ++i) {
        leds[i] = bg;
    }

    // Overlay beat additively — red decays to black, never to purple.
    if (beat > 0) {
        CRGB beatColor = CHSV(0, 220, beat);
        for (uint16_t i = 0; i < NUM_LEDS; ++i) {
            leds[i] += beatColor;
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