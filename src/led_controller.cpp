#include "led_controller.h"

CRGB leds[NUM_LEDS];

void setupLedController() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear();
    FastLED.show();
}

void drawRainbow(uint8_t hueOffset) {
    for (uint16_t i = 0; i < NUM_LEDS; ++i) {
        leds[i] = CHSV((i * 10 + hueOffset) % 255, 255, 255);
    }
}

void showLeds() {
    FastLED.show();
}

void clearLeds() {
    FastLED.clear();
}
