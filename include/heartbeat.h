#pragma once

#include <FastLED.h>
#include "led_controller.h"

void setupHeartbeat();
uint8_t heartbeatBrightness();
void applyHeartbeatPulse(uint8_t pulse);
