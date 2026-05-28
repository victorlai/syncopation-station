#include <Arduino.h>
#include "led_controller.h"
#include "heartbeat.h"
#include "connection_pulse.h"
#include "sync.h"

void setup() {
    setupLedController();
    setupHeartbeat();
}

void loop() {
    // Returns a decaying brightness value (220→0 over BEAT_DECAY_MS) on each confirmed beat.
    uint8_t pulse = heartbeatBrightness();
    bool contact   = getContactGood();
    bool confirmed = isContactConfirmed();
    uint8_t bpm    = (uint8_t)constrain(getStableBPM(), 0, 255);

    // Red state and beat flash only activate once a real stable BPM is detected.
    // Prevents boot red flash and red persisting when the sensor has no real heartbeat signal.
    // Visual flow: purple (idle) → dark (finger on, gathering beats) → red (heartbeat confirmed).
    bool heartbeatActive = confirmed && (bpm > 0);

    // Sync timing — tracks how long both participants have held confirmed contact simultaneously.
    // When Person B is added: updateSyncState(confirmed, confirmedB);
    updateSyncState(confirmed, false);

    updatePulses();
    drawFrame(contact, heartbeatActive, bpm);

    // Uniform beat flash across the full strip (single-sensor mode).
    // When Person B is wired, limit to leds[0..NUM_LEDS/2 - 1] for Person A only.
    if (heartbeatActive && pulse > 0) {
        for (uint16_t i = 0; i < NUM_LEDS; i++) leds[i] += CRGB(pulse, 0, 0);
    }

    // Connection pulse (traveling blob) reserved for two-sensor mode.
    // Person B: if (getJustBeat() && confirmed) spawnPulse(LEFT_TO_RIGHT, 220);
    //           if (getJustBeatB() && confirmedB) spawnPulse(RIGHT_TO_LEFT, 220);
    //           if (heartbeatActive || heartbeatActiveB) drawConnectionPulse();

    if (heartbeatActive) drawSyncBloom(isSyncPossible());

    showLeds();
    delay(10);
}
