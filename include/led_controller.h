#pragma once

#include <FastLED.h>

constexpr uint8_t LED_PIN = D2;
constexpr uint16_t NUM_LEDS = 60;
constexpr uint8_t BRIGHTNESS = 5;

extern CRGB leds[NUM_LEDS];

void setupLedController();
void drawRainbow(uint8_t hueOffset);
void showLeds();
void clearLeds();
