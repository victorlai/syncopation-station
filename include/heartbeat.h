#pragma once

#include <FastLED.h>
#include "led_controller.h"

// Heartbeat sensor setup
void setupHeartbeat();

// Returns heartbeat-based brightness value
uint8_t heartbeatBrightness();

// Applies heartbeat pulse effect
void applyHeartbeatPulse(uint8_t pulse);

// Returns stabilized BPM estimate
int getStableBPM();

// Returns true if the sensor currently has good finger contact
bool getContactGood();