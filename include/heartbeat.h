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

// Returns true only after contact has been held for CONTACT_CONFIRM_MS — use this to gate visuals
bool isContactConfirmed();

// Returns true once per valid beat, then resets — use to trigger pulse spawning
bool getJustBeat();

// Returns how many valid beats have been collected since last contact loss
int getValidBeatCount();

// Returns true after two consecutive near-zero raw samples — earliest hint of a finger touch.
// Stays true for a few seconds to cover the contact confirmation window.
bool isPossibleContact();

// Interactive calibration routine — call from setup() or trigger via 'c' in serial monitor.
// Samples no-finger noise floor then 3 with-finger rounds, prints recommended CONTACT_MIN_RANGE.
void runCalibration();