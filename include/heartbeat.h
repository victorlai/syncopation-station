#pragma once

#include <FastLED.h>
#include "led_controller.h"

void setupHeartbeat();
void runCalibration();
void applyHeartbeatPulse(uint8_t pulse);

// ── Sensor A (D3) ──────────────────────────────────────────────────────────────
uint8_t heartbeatBrightness();
bool    getContactGood();
bool    isContactConfirmed();
bool    isPossibleContact();
bool    getJustBeat();
int     getValidBeatCount();
int     getStableBPM();

// ── Sensor B (D8) ──────────────────────────────────────────────────────────────
// Call heartbeatBrightnessB() each loop after heartbeatBrightness() — it also
// triggers the two-column serial print once both sensor caches are populated.
uint8_t heartbeatBrightnessB();
bool    getContactGoodB();
bool    isContactConfirmedB();
bool    isPossibleContactB();
bool    getJustBeatB();
int     getValidBeatCountB();
int     getStableBPMB();
