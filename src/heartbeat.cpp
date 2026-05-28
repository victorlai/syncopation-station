#include "heartbeat.h"
#include <Arduino.h>
#include <PulseSensorPlayground.h>

// Pulse sensor input pin on the ESP32.
static const int PulseWire = D3;

// Beat detection threshold for the 12-bit ADC (range 0–4095).
// Raise if you get false beats (noise). Lower if real beats are missed.
// Start at 1930 and adjust in steps of 50 while watching serial output.
static const int Threshold = 1930;

// LED used by PulseSensorPlayground to blink on each beat.
static const int PulseLed = LED_BUILTIN;

// ANSI color codes for serial monitor output.
static const char* COLOR_RESET  = "\x1b[0m";
static const char* COLOR_RED    = "\x1b[31m";
static const char* COLOR_GREEN  = "\x1b[32m";
static const char* COLOR_YELLOW = "\x1b[33m";
static const char* COLOR_ORANGE = "\x1b[38;5;208m";

// Library object used to interact with PulseSensorPlayground.
static PulseSensorPlayground pulseSensor;

// Print status every 500ms for easier readability.
static const unsigned long RAW_PRINT_INTERVAL_MS = 500;
static unsigned long lastRawPrint = 0;

// Rolling signal window for contact quality — checks how much the signal swings over ~400ms.
// A floating/idle sensor sits flat; a real heartbeat produces visible variation.
static const int CONTACT_WINDOW = 40;       // 40 samples × 10ms loop = 400ms
static const int CONTACT_MIN_RANGE = 200;         // ADC counts of swing required to count as contact. Raise if false GOOD in bright/noisy rooms. Lower if real beats are missed.
static const float CONTACT_DEVIATION_THRESHOLD = 200.0f; // ADC counts shift from idle baseline that also counts as contact. Fires immediately on touch without waiting for variance to build up.
static float rawBaseline = 2048.0f; // Slow EMA of the idle (no-finger) RAW level — self-calibrates to any room.
static int contactSamples[CONTACT_WINDOW];
static int contactIdx = 0;
static bool contactWindowFull = false;

// Minimum and maximum accepted beat interval.
// This prevents fast false beats from showing as high BPM.
static const int MIN_ACCEPTABLE_IBI_MS = 428;   // 140 BPM max // 500 is 120 BP Max
static const int MAX_ACCEPTABLE_IBI_MS = 1500;  // 40 BPM min

// Rolling BPM average — requires this many consecutive valid beats before
// reporting a stable BPM. Raise to be more conservative, lower to respond faster.
static const int BPM_HISTORY_SIZE = 4;   // Valid beats needed before reporting stable BPM. Lower = faster response, higher = more stable.
static int bpmHistory[BPM_HISTORY_SIZE] = {0};
static int bpmHistoryIndex = 0;
static int bpmHistoryCount = 0;

// If no valid beat arrives within this window, reset the average (contact lost).
static const unsigned long BPM_RESET_TIMEOUT_MS = 8000;
static unsigned long lastValidBeatTime = 0;

// Brightness decay after each beat — flash lasts this many ms then fades to 0.
static const unsigned long BEAT_DECAY_MS = 600;
static unsigned long lastBeatMs = 0;
static uint8_t beatPeak = 0;

// Exposed contact state for LED transitions.
static bool lastContactGood = false;

// Set true for one frame when a valid beat is confirmed. Read via getJustBeat().
static bool justBeatFlag = false;

// How long to hold "contact good" after signal goes POOR — absorbs finger shifts.
// Raise if transitions to purple feel too trigger-happy. Lower if you want faster response.
static const unsigned long CONTACT_HOLD_MS = 2500;
static unsigned long lastContactGoodTime = 0;

// How long contact must be held before the strip commits to sensing state (red + pulses).
// Prevents false positives from triggering colour changes. Adjust to taste.
static const unsigned long CONTACT_CONFIRM_MS = 2000;
static unsigned long contactStartMs = 0;
static bool prevContactGood = false;

// Calibration routine — triggered via 'c' in the serial monitor, or called from setupHeartbeat().
// Phase 1: one no-finger baseline (noise floor).
// Phase 2: NUM_CAL_ROUNDS with-finger samples — lift and replace between each for variability.
// Recommends CONTACT_MIN_RANGE as the midpoint between noise ceiling and worst finger minimum.
void runCalibration() {
    const int  NUM_CAL_ROUNDS    = 3;
    const int  SAMPLE_INTERVAL   = 10;
    const int  BASELINE_DURATION = 5000;
    const int  FINGER_DURATION   = 4000;
    const int  PRINT_INTERVAL    = 500;

    static int calSamples[CONTACT_WINDOW];

    auto sampleRange = [&](int durationMs, int* outMin, int* outMax, bool showCountdown = false) {
        int calIdx = 0;
        for (int i = 0; i < CONTACT_WINDOW; i++) {
            calSamples[i] = analogRead(PulseWire);
            delay(SAMPLE_INTERVAL);
        }
        int rMin = 4095, rMax = 0;
        unsigned long end      = millis() + durationMs;
        unsigned long warmupEnd = millis() + 1000; // skip first 1s — ring buffer needs to fill with real data before rMin is meaningful
        unsigned long nextPrint = millis();
        while (millis() < end) {
            int raw = analogRead(PulseWire);
            calSamples[calIdx] = raw;
            calIdx = (calIdx + 1) % CONTACT_WINDOW;
            int cMin = calSamples[0], cMax = calSamples[0];
            for (int i = 1; i < CONTACT_WINDOW; i++) {
                if (calSamples[i] < cMin) cMin = calSamples[i];
                if (calSamples[i] > cMax) cMax = calSamples[i];
            }
            int range = cMax - cMin;
            if (millis() >= warmupEnd && range < rMin) rMin = range; // only track rMin after warmup
            if (range > rMax) rMax = range;
            if (millis() >= nextPrint) {
                Serial.printf("  RAW: %4d | RANGE: %4d", raw, range);
                if (showCountdown) {
                    unsigned long rem = (end > millis()) ? end - millis() : 0;
                    Serial.printf(" | lift in %d.%ds", (int)(rem / 1000), (int)((rem % 1000) / 100));
                }
                Serial.println();
                nextPrint += PRINT_INTERVAL;
            }
            delay(SAMPLE_INTERVAL);
        }
        if (outMin) *outMin = rMin;
        if (outMax) *outMax = rMax;
    };

    Serial.println();
    Serial.println("=== CONTACT CALIBRATION ===");
    Serial.print("Rounds: "); Serial.println(NUM_CAL_ROUNDS);
    Serial.println();

    // ── Phase 1: noise floor (once) ──────────────────────────────────────────
    Serial.println("PHASE 1 — Keep finger OFF. Measuring noise floor.");
    for (int i = 3; i >= 1; i--) {
        Serial.print("  "); Serial.print(i); Serial.println("...");
        delay(1000);
    }
    int noFingerMax = 0;
    sampleRange(BASELINE_DURATION, nullptr, &noFingerMax);
    Serial.print("Noise floor max RANGE: "); Serial.println(noFingerMax);
    Serial.println();

    // ── Phase 2: with-finger, NUM_CAL_ROUNDS rounds ──────────────────────────
    int roundMin[NUM_CAL_ROUNDS], roundMax[NUM_CAL_ROUNDS];

    for (int r = 0; r < NUM_CAL_ROUNDS; r++) {
        Serial.print("ROUND "); Serial.print(r + 1);
        Serial.print("/"); Serial.print(NUM_CAL_ROUNDS);
        Serial.println(" — Place finger firmly on sensor.");
        for (int i = 3; i >= 1; i--) {
            Serial.print("  "); Serial.print(i); Serial.println("...");
            delay(1000);
        }
        Serial.println("  Sampling...");
        sampleRange(FINGER_DURATION, &roundMin[r], &roundMax[r], true);
        Serial.print("  Round "); Serial.print(r + 1);
        Serial.print(" RANGE: "); Serial.print(roundMin[r]);
        Serial.print(" – "); Serial.println(roundMax[r]);

        if (r < NUM_CAL_ROUNDS - 1) {
            Serial.println("  Lift finger. Next round in:");
            for (int i = 3; i >= 1; i--) {
                Serial.print("  "); Serial.print(i); Serial.println("...");
                delay(1000);
            }
        }
    }

    // ── Summary ──────────────────────────────────────────────────────────────
    Serial.println();
    Serial.println("=== CALIBRATION RESULT ===");
    Serial.print("Noise floor:  RANGE <= "); Serial.println(noFingerMax);

    int worstFingerMin = 4095;
    int bestFingerMax  = 0;
    for (int r = 0; r < NUM_CAL_ROUNDS; r++) {
        Serial.print("Round "); Serial.print(r + 1);
        Serial.print(":  finger RANGE "); Serial.print(roundMin[r]);
        Serial.print(" – "); Serial.println(roundMax[r]);
        if (roundMin[r] < worstFingerMin) worstFingerMin = roundMin[r];
        if (roundMax[r] > bestFingerMax)  bestFingerMax  = roundMax[r];
    }

    Serial.println();
    // Recommend noise floor + 50-count margin. Heartbeat peaks far exceed this;
    // between-beat valleys rely on the DEV threshold instead.
    int recommended = noFingerMax + 50;
    if (bestFingerMax > noFingerMax * 2) {
        Serial.print("Recommended CONTACT_MIN_RANGE = "); Serial.println(recommended);
        Serial.println("Update heartbeat.cpp and recompile.");
        if (worstFingerMin < noFingerMax) {
            Serial.println("Note: signal dips below noise between beats — normal.");
            Serial.println("DEV threshold handles detection during those valleys.");
        }
    } else {
        Serial.println("WARNING: signal barely above noise floor.");
        Serial.println("Press more firmly, or shield the sensor from ambient light.");
        Serial.print("Current CONTACT_MIN_RANGE = "); Serial.println(CONTACT_MIN_RANGE);
    }
    Serial.println("==========================");
    Serial.println("Press 'c' to run again.");
    Serial.println();
}

void setupHeartbeat() {
    // Start serial output for debugging and monitoring.
    Serial.begin(115200);
    delay(1000);

    // Set ESP32 ADC resolution to 12 bits.
    analogReadResolution(12);

    // Pre-fill contact window with midpoint so range starts at 0 (POOR) until real data arrives.
    for (int i = 0; i < CONTACT_WINDOW; i++) contactSamples[i] = 2048;

    // Configure the pulse sensor input and blink LED behavior.
    pulseSensor.analogInput(PulseWire);
    // pulseSensor.blinkOnPulse(PulseLed); // disabled — timer ISR toggling a pin can cause marginal WS2812B signal corruption
    pulseSensor.setThreshold(Threshold);

    // Initialize the library and confirm startup.
    if (pulseSensor.begin()) {
        Serial.println("We created a pulseSensor Object !");
    } else {
        Serial.println("PulseSensor failed to start.");
    }

    // Uncomment to run calibration on boot instead of using the 'c' key trigger.
    // runCalibration();
}

bool getContactGood() {
    return lastContactGood || (lastContactGoodTime != 0 && millis() - lastContactGoodTime < CONTACT_HOLD_MS);
}

// True only after contact has been held continuously for CONTACT_CONFIRM_MS.
// Use this to gate visual state changes — prevents false positives lighting up the strip.
bool isContactConfirmed() {
    if (!getContactGood()) return false;

    // No real contact has started yet.
    // Prevents millis() - 0 from instantly confirming after boot.
    if (contactStartMs == 0) return false;

    return millis() - contactStartMs >= CONTACT_CONFIRM_MS;
}

int getValidBeatCount() { return bpmHistoryCount; }

// Returns true exactly once per valid beat, then resets. Use to trigger pulse spawning.
bool getJustBeat() {
    bool result = justBeatFlag;
    justBeatFlag = false;
    return result;
}
// Returns the averaged BPM once enough consecutive valid beats are collected.
// Returns 0 if still warming up or if contact has been lost.
int getStableBPM() {
    if (bpmHistoryCount < BPM_HISTORY_SIZE) return 0;
    if (millis() - lastValidBeatTime > BPM_RESET_TIMEOUT_MS) {
        bpmHistoryCount = 0;
        return 0;
    }
    int sum = 0;
    for (int i = 0; i < BPM_HISTORY_SIZE; i++) sum += bpmHistory[i];
    return sum / BPM_HISTORY_SIZE;
}

uint8_t heartbeatBrightness() {
    // Default: 0 lets main.cpp fall back to standby breathing glow.
    uint8_t pulse = 0;

    // Get the current timestamp and raw sample for diagnostics.
    unsigned long now = millis();
    int rawSample = pulseSensor.getLatestSample();

    // First-touch signature: the ADC briefly reads near-zero when a finger first makes contact.
    // Print once on the falling edge so the user knows the sensor registered the touch.
    static bool prevRawNearZero = false;
    bool rawNearZero = (rawSample < 50);
    if (rawNearZero && !prevRawNearZero) {
        Serial.print(COLOR_ORANGE);
        Serial.println("👆🏼 Possible contact detected... reading values");
        Serial.print(COLOR_RESET);
    }
    prevRawNearZero = rawNearZero;

    // Heartbeat state values from the PulseSensor library.
    bool justBeat = pulseSensor.sawStartOfBeat();
    bool insideBeat = pulseSensor.isInsideBeat();
    int myBPM = pulseSensor.getBeatsPerMinute();
    int interBeatInterval = pulseSensor.getInterBeatIntervalMs();
    bool bpmReasonable = (interBeatInterval >= MIN_ACCEPTABLE_IBI_MS && interBeatInterval <=MAX_ACCEPTABLE_IBI_MS);

    // Update rolling contact window and compute signal range.
    contactSamples[contactIdx] = rawSample;
    contactIdx = (contactIdx + 1) % CONTACT_WINDOW;
    if (contactIdx == 0) contactWindowFull = true;

    int cMin = contactSamples[0], cMax = contactSamples[0];
    int windowSize = contactWindowFull ? CONTACT_WINDOW : contactIdx;
    for (int i = 1; i < windowSize; i++) {
        if (contactSamples[i] < cMin) cMin = contactSamples[i];
        if (contactSamples[i] > cMax) cMax = contactSamples[i];
    }
    // Update the idle baseline only when the signal looks stable (low variance, raw in range).
    // EMA time constant ~1s — adapts to room without tracking heartbeat pulses.
    bool signalIdle = (rawSample > 400) && (cMax - cMin < CONTACT_MIN_RANGE / 2);
    if (signalIdle) rawBaseline = rawBaseline * 0.99f + (float)rawSample * 0.01f;

    // Contact good if: signal in range AND (variance high OR DC has shifted far from idle baseline).
    // Deviation fires immediately on touch; variance confirms sustained contact.
    float deviation = fabsf((float)rawSample - rawBaseline);
    bool contactGood = (rawSample > 400 && rawSample < 3700) &&
                       (cMax - cMin >= CONTACT_MIN_RANGE || deviation >= CONTACT_DEVIATION_THRESHOLD);

    // Suppress contact for the first 1.5s of loop execution — sensor needs time to settle after power-on.
    // Without this, startup transients can set contactStartMs and trigger a false confirmation 2s later.
    static unsigned long startupProtectUntil = 0;
    if (startupProtectUntil == 0) startupProtectUntil = now + 2500;
    if (now < startupProtectUntil) contactGood = false;

    // Start the confirmation timer only on a genuine rising edge — when contact was fully absent.
    // The contactStartMs == 0 guard prevents brief signal dips (which happen naturally between
    // heartbeat peaks) from restarting the timer. The hold timer handles those micro-drops.
    if (contactGood && !prevContactGood && contactStartMs == 0) {
        contactStartMs = now;
    }
    // Fully reset confirmation timing after contact has been gone longer than the hold window.
    // Prevents stale timestamps from causing instant re-confirmation from sensor noise later.
    if (!contactGood && millis() - lastContactGoodTime >= CONTACT_HOLD_MS) {
        contactStartMs = 0;
    }

prevContactGood = contactGood;
    
    lastContactGood = contactGood;
    if (contactGood) lastContactGoodTime = now;

    if (now - lastRawPrint >= RAW_PRINT_INTERVAL_MS) {
        Serial.print("CONTACT: ");
        Serial.print(contactGood ? COLOR_GREEN : COLOR_RED);
        Serial.print(contactGood ? "GOOD" : "POOR");
        Serial.print(COLOR_RESET);
        Serial.printf(" | RAW: %4d | RANGE: %4d | DEV: %4d | IBI: ",
                      rawSample, cMax - cMin, (int)deviation);
        if (bpmReasonable && myBPM > 0) Serial.printf("%4d", interBeatInterval);
        else                            Serial.print(" ---");
        Serial.print("ms | BPM: ");
        if (bpmReasonable && myBPM > 0) Serial.printf("%3d", myBPM);
        else                            Serial.print("---");
        // Hold timer at end — doesn't affect column alignment of the fixed-width values.
        bool holdActive = !contactGood && lastContactGoodTime != 0
                          && (now - lastContactGoodTime) < CONTACT_HOLD_MS;
        if (holdActive) {
            unsigned long remaining = CONTACT_HOLD_MS - (now - lastContactGoodTime);
            Serial.print(COLOR_YELLOW);
            Serial.printf(" | hold %d.%ds",
                          (int)(remaining / 1000UL), (int)((remaining % 1000) / 100));
            Serial.print(COLOR_RESET);
        }
        Serial.println();
        lastRawPrint = now;
    }

    // Only count beats when contact is confirmed — prevents noise and floating-pin false beats.
    if (justBeat && isContactConfirmed()) {
        if (bpmReasonable) {
            bpmHistory[bpmHistoryIndex] = myBPM; // Add to rolling BPM history for averaging
            bpmHistoryIndex = (bpmHistoryIndex + 1) % BPM_HISTORY_SIZE; 
            if (bpmHistoryCount < BPM_HISTORY_SIZE) bpmHistoryCount++; // Track how many valid beats we've seen for stable BPM calculation
            lastValidBeatTime = now;
            justBeatFlag = true;

            int stable = getStableBPM();
            static int prevStableBpm = 0;
            Serial.print(COLOR_RED);
            Serial.printf("♥  Beat detected! | RAW: %4d | BPM: %3d", rawSample, myBPM);
            if (stable > 0) {
                Serial.printf(" | ♥ Stable BPM: %3d", stable);
                if      (prevStableBpm > 0 && stable > prevStableBpm) Serial.print(" ⬆");
                else if (prevStableBpm > 0 && stable < prevStableBpm) Serial.print(" ⬇");
                prevStableBpm = stable;
            } else {
                Serial.printf(" | Gathering beats (%d/%d)", bpmHistoryCount, BPM_HISTORY_SIZE);
            }
            Serial.print(COLOR_RESET);
            Serial.println();
            lastBeatMs = now;
            beatPeak = 220;
        } else {
            Serial.print("⚠️  Beat ignored | IBI: ");
            Serial.print(interBeatInterval);
            Serial.println("ms");
        }
    }

    // Beat animation: clear on any confirmation drop to prevent phantom flashes.
    if (!isContactConfirmed()) {
        beatPeak = 0;
        justBeatFlag = false;
    }
    // BPM history: only reset when contact is fully gone (hold timer expired).
    // Keeping it alive through brief signal dips prevents heartbeatActive from flickering.
    if (!getContactGood()) {
        bpmHistoryCount = 0;
    }

    // Quadratic ease-out decay: drops fast then tapers — more like a real heartbeat pulse.
    // At 50% through: 25% brightness. At 75%: 6%. Much smoother than linear.
    unsigned long elapsed = now - lastBeatMs;
    if (elapsed < BEAT_DECAY_MS) {
        uint32_t remaining = BEAT_DECAY_MS - elapsed;
        pulse = (uint32_t)beatPeak * remaining * remaining / ((uint32_t)BEAT_DECAY_MS * BEAT_DECAY_MS);
    }

    return pulse; // Return the current pulse brightness for this frame; 0 if no beat or after decay.
}

void applyHeartbeatPulse(uint8_t pulse) {
    // Scale all LEDs by the current pulse brightness.
    nscale8_video(leds, NUM_LEDS, pulse);
}
