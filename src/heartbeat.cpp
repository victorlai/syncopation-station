#include "heartbeat.h"
#include <Arduino.h>
#include <PulseSensorPlayground.h>

// ── Configuration ──────────────────────────────────────────────────────────────

static const int   THRESHOLD                   = 1930;
static const int   CONTACT_WINDOW              = 40;   // 40 × 10 ms = 400 ms window
static const int   CONTACT_MIN_RANGE           = 200;
static const float CONTACT_DEVIATION_THRESHOLD = 200.0f;
static const int   MIN_ACCEPTABLE_IBI_MS       = 428;  // 140 BPM max
static const int   MAX_ACCEPTABLE_IBI_MS       = 1500; // 40 BPM min
static const int   BPM_HISTORY_SIZE            = 4;
static const unsigned long CONTACT_HOLD_MS       = 2500;
static const unsigned long CONTACT_CONFIRM_MS    = 2000;
static const unsigned long BPM_RESET_TIMEOUT_MS  = 8000;
static const unsigned long BEAT_DECAY_MS         = 600;
static const unsigned long PRINT_INTERVAL_MS     = 500;

static const char* COLOR_RESET  = "\x1b[0m";
static const char* COLOR_RED    = "\x1b[31m";
static const char* COLOR_GREEN  = "\x1b[32m";
static const char* COLOR_ORANGE = "\x1b[38;5;208m";

static PulseSensorPlayground pulseSensor(2);

// Visible width of one column ("GOOD | RAW:1845 | RNG: 350 | BPM: 72" = 36)
// plus the 4-space separator — B events indent by this many chars to align right.
static const int B_COL_INDENT = 40;

// ── Per-sensor state ───────────────────────────────────────────────────────────

struct SensorState {
    int  pin;
    int  idx;

    int   samples[CONTACT_WINDOW];
    int   sampleIdx;
    bool  windowFull;
    float rawBaseline;

    bool          lastContactGood;
    unsigned long lastContactGoodTime;
    unsigned long contactStartMs;
    bool          prevContactGood;
    unsigned long startupProtectUntil;

    int           nearZeroStreak;
    bool          possibleActive;
    unsigned long possibleUntilMs;

    int           bpmHistory[BPM_HISTORY_SIZE];
    int           bpmHistoryIdx;
    int           bpmHistoryCount;
    unsigned long lastValidBeatTime;

    unsigned long lastBeatMs;
    uint8_t       beatPeak;
    bool          justBeatFlag;

    // print cache — filled each processSensor call, consumed by printColumns
    bool printContactGood;
    int  printRaw;
    int  printRange;
    int  printBpm;   // -1 = no valid reading
};

static SensorState sA, sB;
static unsigned long lastPrintMs = 0;

// ── State helpers ──────────────────────────────────────────────────────────────

static bool cGoodFor(const SensorState& s, unsigned long now) {
    return s.lastContactGood ||
           (s.lastContactGoodTime != 0 && now - s.lastContactGoodTime < CONTACT_HOLD_MS);
}

static bool cConfFor(const SensorState& s, unsigned long now) {
    return cGoodFor(s, now) && s.contactStartMs != 0 &&
           (now - s.contactStartMs >= CONTACT_CONFIRM_MS);
}

static int stableBPMFor(SensorState& s, unsigned long now) {
    if (s.bpmHistoryCount < BPM_HISTORY_SIZE) return 0;
    if (now - s.lastValidBeatTime > BPM_RESET_TIMEOUT_MS) {
        s.bpmHistoryCount = 0;
        return 0;
    }
    int sum = 0;
    for (int i = 0; i < BPM_HISTORY_SIZE; i++) sum += s.bpmHistory[i];
    return sum / BPM_HISTORY_SIZE;
}

// ── Column printing ────────────────────────────────────────────────────────────
// Called after sensor B is processed so both caches are fresh.
// GOOD and POOR are the same byte length with ANSI codes, so columns align.

static void printColumns(unsigned long now) {
    if (now - lastPrintMs < PRINT_INTERVAL_MS) return;
    lastPrintMs = now;

    auto fmtCol = [](const SensorState& s) -> String {
        char buf[64];
        const char* clr = s.printContactGood ? COLOR_GREEN : COLOR_RED;
        const char* lbl = s.printContactGood ? "GOOD" : "POOR";
        if (s.printBpm >= 0)
            snprintf(buf, sizeof(buf), "%s%s%s | RAW:%4d | RNG:%4d | BPM:%3d",
                     clr, lbl, COLOR_RESET, s.printRaw, s.printRange, s.printBpm);
        else
            snprintf(buf, sizeof(buf), "%s%s%s | RAW:%4d | RNG:%4d | BPM:---",
                     clr, lbl, COLOR_RESET, s.printRaw, s.printRange);
        return String(buf);
    };

    Serial.print(fmtCol(sA));
    Serial.print("    ");
    Serial.println(fmtCol(sB));
}

// ── Core processing ────────────────────────────────────────────────────────────

static uint8_t processSensor(SensorState& s, unsigned long now) {
    int rawSample = pulseSensor.getLatestSample(s.idx);

    // Near-zero hint: first-touch signature, require 2 consecutive samples
    if (rawSample < 50) {
        if (++s.nearZeroStreak == 2) {
            s.possibleActive  = true;
            s.possibleUntilMs = now + 4000;
            const char* pin = (s.idx == 0) ? "D3" : "D8";
            if (s.idx == 0)
                Serial.printf("%s👆🏼 Possible contact [%s]%s\n", COLOR_ORANGE, pin, COLOR_RESET);
            else
                Serial.printf("%*s%s👆🏼 Possible contact [%s]%s\n",
                              B_COL_INDENT, "", COLOR_ORANGE, pin, COLOR_RESET);
        }
    } else {
        s.nearZeroStreak = 0;
    }

    bool justBeat = pulseSensor.sawStartOfBeat(s.idx);
    int  myBPM    = pulseSensor.getBeatsPerMinute(s.idx);
    int  ibi      = pulseSensor.getInterBeatIntervalMs(s.idx);
    bool bpmOK    = (ibi >= MIN_ACCEPTABLE_IBI_MS && ibi <= MAX_ACCEPTABLE_IBI_MS);

    // Rolling contact window — min/max range over ~400 ms
    s.samples[s.sampleIdx] = rawSample;
    s.sampleIdx = (s.sampleIdx + 1) % CONTACT_WINDOW;
    if (s.sampleIdx == 0) s.windowFull = true;

    int cMin = s.samples[0], cMax = s.samples[0];
    int wSize = s.windowFull ? CONTACT_WINDOW : (s.sampleIdx > 0 ? s.sampleIdx : 1);
    for (int i = 1; i < wSize; i++) {
        if (s.samples[i] < cMin) cMin = s.samples[i];
        if (s.samples[i] > cMax) cMax = s.samples[i];
    }
    int   range     = cMax - cMin;
    float deviation = fabsf((float)rawSample - s.rawBaseline);

    bool signalIdle = (rawSample > 400) && (range < CONTACT_MIN_RANGE / 2);
    if (signalIdle) s.rawBaseline = s.rawBaseline * 0.99f + (float)rawSample * 0.01f;

    bool contactGood = (rawSample > 400 && rawSample < 3700) &&
                       (range >= CONTACT_MIN_RANGE || deviation >= CONTACT_DEVIATION_THRESHOLD);

    if (s.startupProtectUntil == 0) s.startupProtectUntil = now + 2500;
    if (now < s.startupProtectUntil) contactGood = false;

    if (contactGood && !s.prevContactGood && s.contactStartMs == 0)
        s.contactStartMs = now;
    if (!contactGood && now - s.lastContactGoodTime >= CONTACT_HOLD_MS)
        s.contactStartMs = 0;

    s.prevContactGood = contactGood;
    s.lastContactGood = contactGood;
    if (contactGood) s.lastContactGoodTime = now;

    bool cGood = cGoodFor(s, now);
    bool cConf = cConfFor(s, now);

    // Update print cache
    s.printContactGood = contactGood;
    s.printRaw         = rawSample;
    s.printRange       = range;
    s.printBpm         = (cGood && bpmOK && myBPM > 0) ? myBPM : -1;

    // Beat processing — only when contact is confirmed
    if (justBeat && cConf) {
        if (bpmOK) {
            s.bpmHistory[s.bpmHistoryIdx] = myBPM;
            s.bpmHistoryIdx = (s.bpmHistoryIdx + 1) % BPM_HISTORY_SIZE;
            if (s.bpmHistoryCount < BPM_HISTORY_SIZE) s.bpmHistoryCount++;
            s.lastValidBeatTime = now;
            s.justBeatFlag      = true;
            s.lastBeatMs        = now;
            s.beatPeak          = 220;

            int stable = stableBPMFor(s, now);
            Serial.print(COLOR_RED);
            if (s.idx == 0) {
                if (stable > 0)
                    Serial.printf("♥  BPM:%3d | Stable:%3d\n", myBPM, stable);
                else
                    Serial.printf("♥  BPM:%3d | Gathering beats (%d/%d)\n",
                                  myBPM, s.bpmHistoryCount, BPM_HISTORY_SIZE);
            } else {
                if (stable > 0)
                    Serial.printf("%*s♥  BPM:%3d | Stable:%3d\n",
                                  B_COL_INDENT, "", myBPM, stable);
                else
                    Serial.printf("%*s♥  BPM:%3d | Gathering beats (%d/%d)\n",
                                  B_COL_INDENT, "", myBPM,
                                  s.bpmHistoryCount, BPM_HISTORY_SIZE);
            }
            Serial.print(COLOR_RESET);
        } else {
            if (s.idx == 0)
                Serial.printf("⚠️  Beat ignored | IBI:%dms\n", ibi);
            else
                Serial.printf("%*s⚠️  Beat ignored | IBI:%dms\n",
                              B_COL_INDENT, "", ibi);
        }
    }

    if (!cConf) { s.beatPeak = 0; s.justBeatFlag = false; }
    if (!cGood)   s.bpmHistoryCount = 0;
    if (s.possibleActive && now > s.possibleUntilMs && !cGood) s.possibleActive = false;

    // Quadratic decay brightness
    uint8_t pulse = 0;
    unsigned long beatElapsed = now - s.lastBeatMs;
    if (beatElapsed < BEAT_DECAY_MS) {
        uint32_t rem = BEAT_DECAY_MS - beatElapsed;
        pulse = (uint32_t)s.beatPeak * rem * rem /
                ((uint32_t)BEAT_DECAY_MS * BEAT_DECAY_MS);
    }
    return pulse;
}

// ── Public API — sensor A ──────────────────────────────────────────────────────

uint8_t heartbeatBrightness()  { return processSensor(sA, millis()); }
bool    getContactGood()       { return cGoodFor(sA, millis()); }
bool    isContactConfirmed()   { return cConfFor(sA, millis()); }
bool    isPossibleContact()    { return sA.possibleActive; }
bool    getJustBeat()          { bool r = sA.justBeatFlag; sA.justBeatFlag = false; return r; }
int     getValidBeatCount()    { return sA.bpmHistoryCount; }
int     getStableBPM()         { return stableBPMFor(sA, millis()); }

// ── Public API — sensor B ──────────────────────────────────────────────────────
// heartbeatBrightnessB() must be called each loop after heartbeatBrightness()
// — it triggers the column print once both caches are populated.

uint8_t heartbeatBrightnessB() {
    unsigned long now = millis();
    uint8_t pulse = processSensor(sB, now);
    printColumns(now);
    return pulse;
}
bool    getContactGoodB()      { return cGoodFor(sB, millis()); }
bool    isContactConfirmedB()  { return cConfFor(sB, millis()); }
bool    isPossibleContactB()   { return sB.possibleActive; }
bool    getJustBeatB()         { bool r = sB.justBeatFlag; sB.justBeatFlag = false; return r; }
int     getValidBeatCountB()   { return sB.bpmHistoryCount; }
int     getStableBPMB()        { return stableBPMFor(sB, millis()); }

void applyHeartbeatPulse(uint8_t pulse) {
    nscale8_video(leds, NUM_LEDS, pulse);
}

// ── Setup ──────────────────────────────────────────────────────────────────────

void setupHeartbeat() {
    Serial.begin(115200);
    delay(1000);
    analogReadResolution(12);

    memset(&sA, 0, sizeof(sA)); sA.pin = D3; sA.idx = 0; sA.rawBaseline = 2048.0f;
    memset(&sB, 0, sizeof(sB)); sB.pin = D8; sB.idx = 1; sB.rawBaseline = 2048.0f;
    for (int i = 0; i < CONTACT_WINDOW; i++) sA.samples[i] = sB.samples[i] = 2048;
    sA.printBpm = sB.printBpm = -1;

    pulseSensor.analogInput(D3, 0);
    pulseSensor.analogInput(D8, 1);
    pulseSensor.setThreshold(THRESHOLD, 0);
    pulseSensor.setThreshold(THRESHOLD, 1);

    if (pulseSensor.begin()) {
        Serial.println("PulseSensor ready — A=D3, B=D8.");
    } else {
        Serial.println("PulseSensor failed to start.");
    }
}

// ── Calibration (D3 + D8) ──────────────────────────────────────────────────────

void runCalibration() {
    const int NUM_CAL_ROUNDS    = 2;
    const int SAMPLE_INTERVAL   = 10;
    const int BASELINE_DURATION = 3000;
    const int FINGER_DURATION   = 2500;
    const int PRINT_INTERVAL    = 500;

    static int bufA[CONTACT_WINDOW], bufB[CONTACT_WINDOW];

    // Sample one pin into a rolling window; return min and max range seen.
    auto sampleOne = [&](int pin, int* buf, int durationMs,
                         int* outMin, int* outMax, bool countdown = false) {
        for (int i = 0; i < CONTACT_WINDOW; i++) { buf[i] = analogRead(pin); delay(SAMPLE_INTERVAL); }
        int idx = 0, rMin = 4095, rMax = 0;
        unsigned long end = millis() + durationMs, warmup = millis() + 1000, nextPrint = millis();
        while (millis() < end) {
            int raw = analogRead(pin);
            buf[idx] = raw; idx = (idx + 1) % CONTACT_WINDOW;
            int cMin = buf[0], cMax = buf[0];
            for (int i = 1; i < CONTACT_WINDOW; i++) {
                if (buf[i] < cMin) cMin = buf[i];
                if (buf[i] > cMax) cMax = buf[i];
            }
            int r = cMax - cMin;
            if (millis() >= warmup && r < rMin) rMin = r;
            if (r > rMax) rMax = r;
            if (millis() >= nextPrint) {
                Serial.printf("  RAW:%4d | RNG:%4d", raw, r);
                if (countdown) {
                    unsigned long rem = (end > millis()) ? end - millis() : 0;
                    Serial.printf(" | lift in %d.%ds", (int)(rem/1000), (int)((rem%1000)/100));
                }
                Serial.println();
                nextPrint += PRINT_INTERVAL;
            }
            delay(SAMPLE_INTERVAL);
        }
        if (outMin) *outMin = rMin;
        if (outMax) *outMax = rMax;
    };

    // Sample both pins simultaneously and print in columns.
    auto sampleBoth = [&](int durationMs, int* outMaxA, int* outMaxB) {
        for (int i = 0; i < CONTACT_WINDOW; i++) {
            bufA[i] = analogRead(D3); bufB[i] = analogRead(D8); delay(SAMPLE_INTERVAL);
        }
        int iA = 0, iB = 0, rMaxA = 0, rMaxB = 0;
        unsigned long end = millis() + durationMs, nextPrint = millis();
        while (millis() < end) {
            int rawA = analogRead(D3), rawB = analogRead(D8);
            bufA[iA] = rawA; iA = (iA + 1) % CONTACT_WINDOW;
            bufB[iB] = rawB; iB = (iB + 1) % CONTACT_WINDOW;
            int cMinA = bufA[0], cMaxA = bufA[0], cMinB = bufB[0], cMaxB = bufB[0];
            for (int i = 1; i < CONTACT_WINDOW; i++) {
                if (bufA[i] < cMinA) cMinA = bufA[i]; if (bufA[i] > cMaxA) cMaxA = bufA[i];
                if (bufB[i] < cMinB) cMinB = bufB[i]; if (bufB[i] > cMaxB) cMaxB = bufB[i];
            }
            int rA = cMaxA - cMinA, rB = cMaxB - cMinB;
            if (rA > rMaxA) rMaxA = rA;
            if (rB > rMaxB) rMaxB = rB;
            if (millis() >= nextPrint) {
                Serial.printf("  D3 RAW:%4d RNG:%4d    D8 RAW:%4d RNG:%4d\n",
                              rawA, rA, rawB, rB);
                nextPrint += PRINT_INTERVAL;
            }
            delay(SAMPLE_INTERVAL);
        }
        if (outMaxA) *outMaxA = rMaxA;
        if (outMaxB) *outMaxB = rMaxB;
    };

    auto printResult = [&](const char* pin, int noiseMax, int* rMin, int* rMax) {
        Serial.printf("  %s  noise <= %d", pin, noiseMax);
        int bestMax = 0;
        for (int r = 0; r < NUM_CAL_ROUNDS; r++) {
            Serial.printf("  |  round %d: %d–%d", r+1, rMin[r], rMax[r]);
            if (rMax[r] > bestMax) bestMax = rMax[r];
        }
        int rec = noiseMax + 50;
        if (bestMax > noiseMax * 2)
            Serial.printf("  →  CONTACT_MIN_RANGE = %d\n", rec);
        else
            Serial.printf("  →  WARNING: signal weak (current %d)\n", CONTACT_MIN_RANGE);
    };

    Serial.println("\n=== CALIBRATION (D3 + D8) ===");

    // ── Phase 1: noise floor — both sensors, no finger ─────────────────────────
    Serial.println("PHASE 1 — Keep both fingers OFF.");
    for (int i = 2; i >= 1; i--) { Serial.printf("  %d...\n", i); delay(1000); }
    int nfA = 0, nfB = 0;
    sampleBoth(BASELINE_DURATION, &nfA, &nfB);
    Serial.printf("Noise floor — D3: %d  D8: %d\n\n", nfA, nfB);

    // ── Phase 2: sensor A (D3) finger rounds ───────────────────────────────────
    int minA[NUM_CAL_ROUNDS], maxA[NUM_CAL_ROUNDS];
    Serial.println("── D3 (Sensor A) ──");
    for (int r = 0; r < NUM_CAL_ROUNDS; r++) {
        Serial.printf("Round %d/%d — finger on D3.\n", r+1, NUM_CAL_ROUNDS);
        for (int i = 2; i >= 1; i--) { Serial.printf("  %d...\n", i); delay(1000); }
        sampleOne(D3, bufA, FINGER_DURATION, &minA[r], &maxA[r], true);
        Serial.printf("  Round %d RANGE: %d – %d\n", r+1, minA[r], maxA[r]);
        if (r < NUM_CAL_ROUNDS - 1) {
            Serial.println("  Lift. Next in:");
            for (int i = 2; i >= 1; i--) { Serial.printf("  %d...\n", i); delay(1000); }
        }
    }

    // ── Phase 3: sensor B (D8) finger rounds ───────────────────────────────────
    int minB[NUM_CAL_ROUNDS], maxB[NUM_CAL_ROUNDS];
    Serial.println("\n── D8 (Sensor B) ──");
    for (int r = 0; r < NUM_CAL_ROUNDS; r++) {
        Serial.printf("Round %d/%d — finger on D8.\n", r+1, NUM_CAL_ROUNDS);
        for (int i = 2; i >= 1; i--) { Serial.printf("  %d...\n", i); delay(1000); }
        sampleOne(D8, bufB, FINGER_DURATION, &minB[r], &maxB[r], true);
        Serial.printf("  Round %d RANGE: %d – %d\n", r+1, minB[r], maxB[r]);
        if (r < NUM_CAL_ROUNDS - 1) {
            Serial.println("  Lift. Next in:");
            for (int i = 2; i >= 1; i--) { Serial.printf("  %d...\n", i); delay(1000); }
        }
    }

    // ── Results ────────────────────────────────────────────────────────────────
    Serial.println("\n=== RESULT ===");
    printResult("D3:", nfA, minA, maxA);
    printResult("D8:", nfB, minB, maxB);
    Serial.println("==============");
    Serial.println("Update CONTACT_MIN_RANGE in heartbeat.cpp and recompile.");
    Serial.println("Press 'c' to run again.\n");
}
