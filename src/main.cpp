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
    heartbeatBrightness();  // Updates BPM tracking, contact state, and just-beat flag.
    bool contact   = getContactGood();      // Finger detected — triggers immediate visual response.
    bool confirmed = isContactConfirmed();  // Held long enough — triggers red and pulses.
    uint8_t bpm = (uint8_t)constrain(getStableBPM(), 0, 255);

    // Only spawn pulses and switch to red once contact is confirmed — no false positive flickers.
    // Person B: add if (getJustBeatB() && confirmed) spawnPulse(RIGHT_TO_LEFT, 220);
    if (getJustBeat() && confirmed) spawnPulse(LEFT_TO_RIGHT, 220);

    // Sync timing — tracks how long both participants have held confirmed contact simultaneously.
    // Person B stubbed as false until second sensor is wired.
    // When Person B is added: updateSyncState(confirmed, confirmedB);
    updateSyncState(confirmed, false);

    updatePulses();

    drawFrame(contact, confirmed, bpm);
    if (confirmed) drawConnectionPulse();
    if (confirmed) drawSyncBloom(isSyncPossible()); // sync visuals not yet implemented — flag is ready

    showLeds();
    delay(10);
}
