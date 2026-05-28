#pragma once
#include <Arduino.h>

// Direction constants — Person A travels left→right, Person B right→left.
static const int8_t LEFT_TO_RIGHT = +1;
static const int8_t RIGHT_TO_LEFT = -1;

// Spawn a new pulse from the edge in the given direction.
void spawnPulse(int8_t direction, uint8_t brightness);

// Move all active pulses forward one frame. Call once per loop before drawing.
void updatePulses();

// Render all active pulses onto leds[] additively (call after drawFrame).
void drawConnectionPulse();

// Soft centre bloom when both BPMs are synchronised. Pass false to skip.
void drawSyncBloom(bool synced);
