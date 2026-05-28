#include "connection_pulse.h"
#include "led_controller.h"
#include <FastLED.h>
#include <math.h>

// Maximum simultaneous pulses (2 per person is plenty).
static const uint8_t MAX_PULSES = 4;

// LEDs per frame. At 100fps: 0.5 × 100 = 50 LEDs/sec → crosses 60 LEDs in ~1.2s.
// Raise for faster travel, lower for slower.
static const float PULSE_SPEED = 0.5f;

// Controls blob softness. Higher = wider, softer. Lower = tighter, sharper.
// Current value gives a visible glow roughly 10 LEDs wide.
static const float PULSE_SIGMA_SQ = 6.0f;

// LEDs beyond this distance from the pulse centre are skipped (performance + avoids near-zero values).
static const float PULSE_CUTOFF = 7.0f;

struct Pulse {
    float position;
    int8_t direction;
    uint8_t brightness;
    bool active;
};

static Pulse pulses[MAX_PULSES];

void spawnPulse(int8_t direction, uint8_t brightness) {
    float startPos = (direction == LEFT_TO_RIGHT) ? 0.0f : (float)(NUM_LEDS - 1);

    // Use the first inactive slot.
    for (uint8_t i = 0; i < MAX_PULSES; i++) {
        if (!pulses[i].active) {
            pulses[i] = { startPos, direction, brightness, true };
            return;
        }
    }

    // All slots full — overwrite the first active pulse travelling the same direction.
    for (uint8_t i = 0; i < MAX_PULSES; i++) {
        if (pulses[i].direction == direction) {
            pulses[i] = { startPos, direction, brightness, true };
            return;
        }
    }
}

void updatePulses() {
    for (uint8_t i = 0; i < MAX_PULSES; i++) {
        if (!pulses[i].active) continue;
        pulses[i].position += pulses[i].direction * PULSE_SPEED;

        // Each person's pulse travels only within their half of the strip.
        // Person A (LEFT_TO_RIGHT): dies past the centre. Person B (RIGHT_TO_LEFT): dies before it.
        bool offStrip = (pulses[i].direction == LEFT_TO_RIGHT)
            ? pulses[i].position > NUM_LEDS / 2 + PULSE_CUTOFF
            : pulses[i].position < NUM_LEDS / 2 - PULSE_CUTOFF;

        if (offStrip) pulses[i].active = false;
    }
}

void drawConnectionPulse() {
    for (uint8_t p = 0; p < MAX_PULSES; p++) {
        if (!pulses[p].active) continue;

        float center = pulses[p].position;

        // Each pulse only renders within its owner's half.
        // Person A (LEFT_TO_RIGHT): LEDs 0 to mid-1. Person B: mid to NUM_LEDS-1.
        int ledStart = (pulses[p].direction == LEFT_TO_RIGHT) ? 0 : NUM_LEDS / 2;
        int ledEnd   = (pulses[p].direction == LEFT_TO_RIGHT) ? NUM_LEDS / 2 : NUM_LEDS;

        for (int i = ledStart; i < ledEnd; i++) {
            float dist = fabsf((float)i - center);
            if (dist >= PULSE_CUTOFF) continue;

            float falloff = expf(-dist * dist / PULSE_SIGMA_SQ);
            uint8_t contribution = (uint8_t)(pulses[p].brightness * falloff);
            leds[i] += CRGB(contribution, 0, 0);
        }
    }
}

// Soft warm bloom at the centre when both participants are synchronised.
// Currently stubbed — will activate once second sensor is wired and BPMs are compared.
void drawSyncBloom(bool synced) {
    if (!synced) return;

    uint8_t bloomBrightness = beatsin8(8, 20, 160);
    float center = NUM_LEDS / 2.0f;

    for (int i = 0; i < NUM_LEDS; i++) {
        float dist = fabsf((float)i - center);
        float falloff = expf(-dist * dist / 30.0f);  // wider spread than pulse
        uint8_t contribution = (uint8_t)(bloomBrightness * falloff);
        // Warm gold tint: red full, small green component.
        leds[i] += CRGB(contribution, contribution >> 2, 0);
    }
}
