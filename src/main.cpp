#include <Arduino.h>
#include "led_controller.h"
#include "heartbeat.h"
#include "connection_pulse.h"
#include "sync.h"

// ── Brightness toggle button (D5, active-low via internal pull-up) ─────────────
constexpr uint8_t BTN_PIN        = D5;
constexpr uint8_t BRIGHTNESS_DAY = 180;   // filtered daylight / canopy
// BRIGHTNESS (100) from led_controller.h is the night/default level

static bool          btnDayMode   = false;
static bool          btnPrevHigh  = true;
static unsigned long btnLastMs    = 0;

void setup() {
    pinMode(BTN_PIN, INPUT_PULLUP);
    setupLedController();
    setupHeartbeat();
}

void loop() {
    // ── Brightness toggle ─────────────────────────────────────────────────────
    bool btnNow = digitalRead(BTN_PIN);
    if (btnPrevHigh && !btnNow && millis() - btnLastMs > 50) {
        btnDayMode = !btnDayMode;
        FastLED.setBrightness(btnDayMode ? BRIGHTNESS_DAY : BRIGHTNESS);
        btnLastMs = millis();
    }
    btnPrevHigh = btnNow;

    // Sensor A — decaying brightness value on each confirmed beat
    uint8_t pulse  = heartbeatBrightness();
    // Sensor B — also triggers two-column serial print
    uint8_t pulseB = heartbeatBrightnessB();

    // Sensor A state
    bool possibleContact = isPossibleContact();
    bool contact         = getContactGood();
    bool confirmed       = isContactConfirmed();
    int  beatCount       = getValidBeatCount();
    uint8_t bpm          = (uint8_t)constrain(getStableBPM(), 0, 255);

    // Sensor B state
    bool possibleContactB = isPossibleContactB();
    bool contactB         = getContactGoodB();
    bool confirmedB       = isContactConfirmedB();
    int  beatCountB       = getValidBeatCountB();
    uint8_t bpmB          = (uint8_t)constrain(getStableBPMB(), 0, 255);

    // Serial commands: c = calibration, s = sync animation toggle
    if (Serial.available()) {
        char cmd = (char)tolower(Serial.read());
        if (cmd == 'c') runCalibration();
        if (cmd == 's') {
            if (isSyncAnimActive()) cancelSyncAnimation();
            else                    triggerSyncAnimation();
        }
    }

    bool heartbeatActive  = confirmed  && (bpm  > 0);
    bool heartbeatActiveB = confirmedB && (bpmB > 0);
    uint8_t beatPulse  = (heartbeatActive  && pulse  > 0) ? pulse  : 0;
    uint8_t beatPulseB = (heartbeatActiveB && pulseB > 0) ? pulseB : 0;

    // Auto-trigger sync when both have stable BPMs within 10 of each other
    if (!isSyncAnimActive() && bpm > 0 && bpmB > 0) {
        if (abs((int)bpm - (int)bpmB) <= 10) {
            triggerSyncAnimation();
            Serial.print("\x1b[35m");
            Serial.println();
            Serial.println("  ╔══════════════════════════════╗");
            Serial.println("  ║    ✨  HEARTBEATS IN SYNC    ║");
            Serial.printf( "  ║   A [D3] : %3d BPM           ║\n", bpm);
            Serial.printf( "  ║   B [D8] : %3d BPM           ║\n", bpmB);
            Serial.printf( "  ║   diff   : %3d BPM           ║\n", abs((int)bpm - (int)bpmB));
            Serial.println("  ╚══════════════════════════════╝");
            Serial.println();
            Serial.print("\x1b[0m");
        }
    }

    updateSyncState(confirmed, confirmedB);

    updatePulses();
    // Each half of the strip is driven independently by its sensor.
    drawFrame(possibleContact,  contact,  confirmed,  beatCount,  bpm,  beatPulse,
              possibleContactB, contactB, confirmedB, beatCountB, bpmB, beatPulseB);

    // Connection pulses: A travels left→right, B travels right→left
    if (getJustBeat()  && confirmed)  spawnPulse(LEFT_TO_RIGHT, 220);
    if (getJustBeatB() && confirmedB) spawnPulse(RIGHT_TO_LEFT, 220);
    if (heartbeatActive || heartbeatActiveB) drawConnectionPulse();

    if (heartbeatActive || heartbeatActiveB) drawSyncBloom(isSyncPossible());

    showLeds();
    delay(10);
}
