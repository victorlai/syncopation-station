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
    // Press 'c' in the serial monitor to run contact calibration at any time.
    if (Serial.available() && Serial.read() == 'c') runCalibration();

    // Returns a decaying brightness value (220→0 over BEAT_DECAY_MS) on each confirmed beat.
    uint8_t pulse = heartbeatBrightness();
    bool contact   = getContactGood();
    bool confirmed = isContactConfirmed();
    uint8_t bpm    = (uint8_t)constrain(getStableBPM(), 0, 255);

    // Red state and beat flash only activate once a real stable BPM is detected.
    // Prevents boot red flash and red persisting when the sensor has no real heartbeat signal.
    // Visual flow: purple (idle) → dark (finger on, gathering beats) → thermometer fill → pulsing red.
    bool heartbeatActive = confirmed && (bpm > 0);

    // Thermometer fill starts once enough beats are gathered (before stable BPM is required).
    bool connecting = confirmed && (getValidBeatCount() >= CONNECTING_START_BEATS);

    // Sync timing — tracks how long both participants have held confirmed contact simultaneously.
    // When Person B is added: updateSyncState(confirmed, confirmedB);
    updateSyncState(confirmed, false);

    updatePulses();
    // Beat pulse merged into drawFrame so every LED gets one write — eliminates flicker.
    uint8_t beatPulse = (heartbeatActive && pulse > 0) ? pulse : 0;
    drawFrame(contact, confirmed, connecting, bpm, beatPulse);

    // Connection pulse (traveling blob) reserved for two-sensor mode.
    // Person B: if (getJustBeat() && confirmed) spawnPulse(LEFT_TO_RIGHT, 220);
    //           if (getJustBeatB() && confirmedB) spawnPulse(RIGHT_TO_LEFT, 220);
    //           if (heartbeatActive || heartbeatActiveB) drawConnectionPulse();

    if (heartbeatActive) drawSyncBloom(isSyncPossible());

    showLeds();
    delay(10);
}
