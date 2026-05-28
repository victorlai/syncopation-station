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

    bool possibleContact = isPossibleContact();
    bool contact         = getContactGood();
    bool confirmed       = isContactConfirmed();
    int  beatCount       = getValidBeatCount();
    uint8_t bpm          = (uint8_t)constrain(getStableBPM(), 0, 255);

    // beatPulse and heartbeatActive gate the beat-flash; stable BPM required to prevent false flashes.
    bool heartbeatActive = confirmed && (bpm > 0);
    uint8_t beatPulse    = (heartbeatActive && pulse > 0) ? pulse : 0;

    // Sync timing — tracks how long both participants have held confirmed contact simultaneously.
    // When Person B is added: updateSyncState(confirmed, confirmedB);
    updateSyncState(confirmed, false);

    updatePulses();
    drawFrame(possibleContact, contact, confirmed, beatCount, bpm, beatPulse);

    // Connection pulse (traveling blob) reserved for two-sensor mode.
    // Person B: if (getJustBeat() && confirmed) spawnPulse(LEFT_TO_RIGHT, 220);
    //           if (getJustBeatB() && confirmedB) spawnPulse(RIGHT_TO_LEFT, 220);
    //           if (heartbeatActive || heartbeatActiveB) drawConnectionPulse();

    if (heartbeatActive) drawSyncBloom(isSyncPossible());

    showLeds();
    delay(10);
}
