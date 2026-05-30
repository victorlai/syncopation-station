#include "led_controller.h"

// LED data array
CRGB leds[NUM_LEDS];

void setupLedController() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear();
    FastLED.show();
}

// ── Constants ──────────────────────────────────────────────────────────────────

// Each sensor owns one half of the strip.
// Sensor A: leds[0 .. HALF_LEDS-1] (left half, index 0 = strip start).
// Sensor B: leds[NUM_LEDS-1 .. HALF_LEDS] reversed (right half, index 0 = strip end).
constexpr int HALF_LEDS = NUM_LEDS / 2;   // 30

// Starting fill when entering GATHER — 1/6 of a half so there's something to see immediately.
constexpr uint8_t HALF_GATHER_INIT = HALF_LEDS / 6;  // 5

// Must match BPM_HISTORY_SIZE in heartbeat.cpp. Maps beat fraction → half-strip fraction.
constexpr uint8_t GATHER_BEAT_TOTAL = 4;

// ── Per-half animation state ───────────────────────────────────────────────────

enum AnimPhase : uint8_t {
    PHASE_IDLE,
    PHASE_POSSIBLE,
    PHASE_GATHER,
    PHASE_HOLD,
    PHASE_PULSE,
    PHASE_DRAIN,
};

struct HalfAnim {
    AnimPhase     animPhase;
    unsigned long animPhaseStartMs;
    uint8_t       bgLevel;
    float         smoothLitLEDs;
    uint8_t       prevBeatPulse;
    uint32_t      animPeriodMs;
    unsigned long animBeatMs;
    uint16_t      drainLitAtStart;
    int           prevBeatCount;
    float         gatherFillLevel;
    float         gatherPulsePos;
    float         gatherPulseEnd;
    bool          gatherPulseActive;
};

static HalfAnim hA = {};
static HalfAnim hB = {};

// ── Global sync override ───────────────────────────────────────────────────────

static bool          syncActive  = false;
static unsigned long syncStartMs = 0;

// ── Low-level helpers ─────────────────────────────────────────────────────────

// Map half-local index → physical LED.
// rev=false (A, left half):  px(false, i) = leds[i]
// rev=true  (B, right half): px(true,  i) = leds[NUM_LEDS-1-i]
static inline CRGB& px(bool rev, int i) {
    return rev ? leds[NUM_LEDS - 1 - i] : leds[i];
}

static void fillHalf(bool rev, CRGB color) {
    for (int i = 0; i < HALF_LEDS; i++) px(rev, i) = color;
}

// Render a graduated red fill within one half (index 0 = near edge, grows toward centre).
// blinkLeading: last 3 positions animate as a VU meter — signals "waiting for next beat".
static void renderHalfGather(bool rev, uint16_t litCount, bool blinkLeading = false) {
    uint16_t solidCount = (blinkLeading && litCount >= 3) ? litCount - 3 : litCount;
    uint8_t  meterLit   = blinkLeading ? beatsin8(70, 0, 3) : 0;

    static const uint8_t mbri[3] = { 100, 160, 220 };

    for (int i = 0; i < HALF_LEDS; i++) {
        if ((uint16_t)i < solidCount) {
            uint8_t bright = (litCount > 1)
                ? (uint8_t)map(i, 0, (int)(litCount - 1), 40, 180)
                : 140;
            px(rev, i) = CRGB(bright, 0, 0);
        } else if ((uint16_t)i < solidCount + meterLit) {
            px(rev, i) = CRGB(mbri[i - solidCount], 0, 0);
        } else {
            px(rev, i) = CRGB::Black;
        }
    }
}

static void renderSyncAnimation(uint32_t elapsed, uint8_t bpm);   // defined below drawFrame

// ── Per-half state machine + renderer ─────────────────────────────────────────
//
// rev=false → sensor A drives leds[0..HALF_LEDS-1].
// rev=true  → sensor B drives leds[NUM_LEDS-1..HALF_LEDS] (reversed so index 0 is B's near end).
//
static void updateAndRenderHalf(HalfAnim& h, bool rev,
    bool possibleContact, bool contact, bool confirmed,
    int beatCount, uint8_t bpm, uint8_t beatPulse)
{
    unsigned long now = millis();

    // ── Smooth LED fill update ────────────────────────────────────────────────
    const float riseSpeed = (float)HALF_LEDS * 10.0f / CONNECTING_FILL_MS;
    float targetLit = 0.0f;
    if (h.animPhase == PHASE_GATHER) {
        if (bpm > 0) {
            targetLit = (float)HALF_LEDS;
        } else {
            h.smoothLitLEDs = h.gatherFillLevel;
            targetLit       = h.gatherFillLevel;
        }
    } else if (h.animPhase == PHASE_HOLD || h.animPhase == PHASE_PULSE) {
        targetLit = (float)HALF_LEDS;
    }
    if (h.smoothLitLEDs < targetLit) h.smoothLitLEDs = min(h.smoothLitLEDs + riseSpeed, targetLit);
    else                              h.smoothLitLEDs = max(h.smoothLitLEDs - riseSpeed, targetLit);

    // ── Beat detection ────────────────────────────────────────────────────────
    bool beatJustFired = (h.animPhase == PHASE_PULSE) && (beatPulse > 100) && (h.prevBeatPulse < 50);
    h.prevBeatPulse = beatPulse;
    if (beatJustFired && bpm > 0) {
        h.animBeatMs = now;
        uint32_t target = 60000UL / (uint32_t)bpm;
        h.animPeriodMs = (h.animPeriodMs * 4 + target) / 5;
    }

    // ── GATHER: new beat → spawn 3-LED traveling pulse ────────────────────────
    bool gatherBeatFired = (h.animPhase == PHASE_GATHER) && (bpm == 0) && (beatCount > h.prevBeatCount);
    h.prevBeatCount = beatCount;
    if (gatherBeatFired) {
        h.gatherFillLevel = h.gatherPulseEnd;
        h.gatherPulsePos  = h.gatherFillLevel;
        h.gatherPulseEnd  = min((float)HALF_LEDS,
            (float)beatCount / (float)GATHER_BEAT_TOTAL * (float)HALF_LEDS);
        h.gatherPulseActive = true;
    }
    if (h.animPhase == PHASE_GATHER && h.gatherPulseActive) {
        h.gatherPulsePos = min(h.gatherPulsePos + 0.5f, h.gatherPulseEnd);
        if (h.gatherPulsePos >= h.gatherPulseEnd) {
            h.gatherFillLevel   = h.gatherPulseEnd;
            h.gatherPulseActive = false;
        }
    }

    // ── Connection level (PULSE only) ─────────────────────────────────────────
    uint32_t timeSinceLastBeat = (uint32_t)(now - h.animBeatMs);
    uint32_t degradeStartMs    = h.animPeriodMs + h.animPeriodMs / 2;
    uint32_t degradeFullMs     = h.animPeriodMs * 4;
    float connectionLevel = 1.0f;
    if (h.animPhase == PHASE_PULSE && timeSinceLastBeat > degradeStartMs) {
        connectionLevel = (timeSinceLastBeat >= degradeFullMs) ? 0.0f
            : 1.0f - (float)(timeSinceLastBeat - degradeStartMs)
                    / (float)(degradeFullMs - degradeStartMs);
    }

    // ── Phase transitions ─────────────────────────────────────────────────────
    switch (h.animPhase) {

        case PHASE_IDLE:
            if (possibleContact || contact) {
                h.bgLevel = (h.bgLevel > 1) ? h.bgLevel - 1 : 0;
            } else {
                h.bgLevel = (h.bgLevel < 252) ? h.bgLevel + 5 : 255;
            }
            if (confirmed) {
                h.animPhase         = PHASE_GATHER;
                h.bgLevel           = 0;
                h.smoothLitLEDs     = (float)HALF_GATHER_INIT;
                h.gatherFillLevel   = (float)HALF_GATHER_INIT;
                h.gatherPulsePos    = (float)HALF_GATHER_INIT;
                h.gatherPulseEnd    = (float)HALF_GATHER_INIT;
                h.gatherPulseActive = false;
                h.prevBeatCount     = beatCount;
            } else if (possibleContact) {
                h.animPhase = PHASE_POSSIBLE;
            }
            break;

        case PHASE_POSSIBLE:
            if (h.bgLevel > 0) h.bgLevel = (h.bgLevel > 1) ? h.bgLevel - 1 : 0;
            if (confirmed) {
                h.animPhase         = PHASE_GATHER;
                h.bgLevel           = 0;
                h.smoothLitLEDs     = (float)HALF_GATHER_INIT;
                h.gatherFillLevel   = (float)HALF_GATHER_INIT;
                h.gatherPulsePos    = (float)HALF_GATHER_INIT;
                h.gatherPulseEnd    = (float)HALF_GATHER_INIT;
                h.gatherPulseActive = false;
                h.prevBeatCount     = beatCount;
            } else if (!possibleContact && !contact) {
                h.animPhase = PHASE_IDLE;
            }
            break;

        case PHASE_GATHER:
            if (!confirmed && !contact && !possibleContact) {
                h.animPhase = PHASE_IDLE;
                h.bgLevel   = 0;
            } else if (!confirmed && (contact || possibleContact)) {
                h.animPhase = PHASE_POSSIBLE;
                h.bgLevel   = 0;
            } else if (bpm > 0 && h.smoothLitLEDs >= (float)HALF_LEDS - 0.5f) {
                h.smoothLitLEDs    = (float)HALF_LEDS;
                h.animPhase        = PHASE_HOLD;
                h.animPhaseStartMs = now;
            }
            break;

        case PHASE_HOLD:
            if (!confirmed) {
                h.drainLitAtStart  = HALF_LEDS;
                h.animPhase        = PHASE_DRAIN;
                h.animPhaseStartMs = now;
            } else if (now - h.animPhaseStartMs >= CONNECTING_HOLD_MS) {
                h.animPhase  = PHASE_PULSE;
                h.animBeatMs = now;
            }
            break;

        case PHASE_PULSE:
            if (!confirmed) {
                h.drainLitAtStart  = max((uint16_t)1,
                                         (uint16_t)(connectionLevel * HALF_LEDS + 0.5f));
                h.animPhase        = PHASE_DRAIN;
                h.animPhaseStartMs = now;
            } else if (bpm == 0) {
                float resumeLevel   = max((float)HALF_GATHER_INIT,
                                           connectionLevel * (float)HALF_LEDS);
                h.smoothLitLEDs     = resumeLevel;
                h.gatherFillLevel   = resumeLevel;
                h.gatherPulsePos    = resumeLevel;
                h.gatherPulseEnd    = resumeLevel;
                h.gatherPulseActive = false;
                h.prevBeatCount     = beatCount;
                h.animPhase         = PHASE_GATHER;
            }
            break;

        case PHASE_DRAIN:
            if (confirmed) {
                h.animPhase  = PHASE_PULSE;
                h.animBeatMs = now;
            } else if (now - h.animPhaseStartMs >= DRAIN_MS) {
                h.animPhase     = PHASE_IDLE;
                h.bgLevel       = 0;
                h.smoothLitLEDs = 0.0f;
                h.prevBeatPulse = 0;
            }
            break;
    }

    // ── Rendering ─────────────────────────────────────────────────────────────
    switch (h.animPhase) {

        case PHASE_IDLE: {
            // Both halves share the same beatsin8 value → scanners mirror each other,
            // sweeping toward and away from the centre together.
            uint8_t scanPos = beatsin8(30, 0, HALF_LEDS - 1);
            fillHalf(rev, CRGB::Black);
            static const int8_t  off[5] = { -2, -1,  0,  1,  2 };
            static const uint8_t bri[5] = {  12, 45, 110, 45, 12 };
            for (uint8_t j = 0; j < 5; j++) {
                int16_t idx = (int16_t)scanPos + off[j];
                if (idx >= 0 && idx < HALF_LEDS) px(rev, idx) = CRGB(bri[j], 0, 0);
            }
            break;
        }

        case PHASE_POSSIBLE:
            renderHalfGather(rev, 3);
            break;

        case PHASE_GATHER:
            if (bpm > 0) {
                renderHalfGather(rev, (uint16_t)h.smoothLitLEDs, false);
            } else {
                renderHalfGather(rev, (uint16_t)h.gatherFillLevel, !h.gatherPulseActive);
                if (h.gatherPulseActive) {
                    auto head = (int16_t)h.gatherPulsePos;
                    static const int8_t  poff[3] = { -2, -1,  0 };
                    static const uint8_t pbri[3] = { 80, 150, 230 };
                    for (uint8_t j = 0; j < 3; j++) {
                        int16_t idx = head + poff[j];
                        if (idx >= 0 && idx < HALF_LEDS) px(rev, idx) = CRGB(pbri[j], 0, 0);
                    }
                }
            }
            break;

        case PHASE_HOLD:
            fillHalf(rev, CRGB(200, 0, 0));
            break;

        case PHASE_PULSE:
            if (connectionLevel < 1.0f) {
                uint16_t litCount = max((uint16_t)1,
                                        (uint16_t)(connectionLevel * HALF_LEDS + 0.5f));
                renderHalfGather(rev, litCount);
            } else {
                uint32_t elapsed   = timeSinceLastBeat;
                if (elapsed > h.animPeriodMs) elapsed = h.animPeriodMs;
                uint32_t remaining = h.animPeriodMs - elapsed;
                uint8_t  decay     = (uint8_t)((uint32_t)220 * remaining * remaining /
                                               ((uint32_t)h.animPeriodMs * h.animPeriodMs));
                uint8_t  finalRed  = qadd8(decay, 25);
                fillHalf(rev, CRGB(finalRed, 0, 0));
            }
            break;

        case PHASE_DRAIN: {
            uint32_t elapsed  = (uint32_t)(now - h.animPhaseStartMs);
            uint16_t litCount = (elapsed < DRAIN_MS)
                ? (uint16_t)((uint32_t)h.drainLitAtStart * (DRAIN_MS - elapsed) / DRAIN_MS)
                : 0;
            for (int i = 0; i < HALF_LEDS; i++)
                px(rev, i) = ((uint16_t)i < litCount) ? CRGB(180, 0, 0) : CRGB::Black;
            break;
        }
    }
}

// ── Main frame function ───────────────────────────────────────────────────────

void drawFrame(
    bool possibleContactA, bool contactA, bool confirmedA, int beatCountA, uint8_t bpmA, uint8_t beatPulseA,
    bool possibleContactB, bool contactB, bool confirmedB, int beatCountB, uint8_t bpmB, uint8_t beatPulseB)
{
    // SYNC is a full-strip override — bypasses per-half rendering.
    if (syncActive) {
        uint32_t elapsed = (uint32_t)(millis() - syncStartMs);
        if (elapsed >= 27600) {
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            syncActive        = false;
            hA.animPhase      = PHASE_IDLE;
            hB.animPhase      = PHASE_IDLE;
            hA.bgLevel        = 0;
            hB.bgLevel        = 0;
            hA.smoothLitLEDs  = 0.0f;
            hB.smoothLitLEDs  = 0.0f;
        } else {
            uint8_t bpm = (bpmA > 0) ? bpmA : bpmB;
            renderSyncAnimation(elapsed, bpm);
        }
        return;
    }

    updateAndRenderHalf(hA, false,
        possibleContactA, contactA, confirmedA, beatCountA, bpmA, beatPulseA);
    updateAndRenderHalf(hB, true,
        possibleContactB, contactB, confirmedB, beatCountB, bpmB, beatPulseB);
}

// ── Sync animation ─────────────────────────────────────────────────────────────
//
//  Timeline (27.6 s total):
//   0 –  3 s      PULSES   4 full-strip red flashes
//   3 –  4.6 s    WIPE     LEDs turn off from ends inward to centre 3
//   4.6 –  7.6 s  SHIFT    Centre 3 transitions red → orange → yellow → magenta
//   7.6 – 10.6 s  STORM    Frantic magenta sparks scatter across full strip
//  10.6 – 13.6 s  ZAPS     10 directional sparks shoot from centre to both ends
//  13.6 – 14.6 s  MERGE    Crossfade from zap state into pulsing red
//  14.6 – 19.6 s  GLOW     Full strip pulses to participant's BPM
//  19.6 – 24.6 s  FADE     Slow fade to black
//  24.6 – 27.6 s  DARK     3 s silence before returning to IDLE
//
static void renderSyncAnimation(uint32_t elapsed, uint8_t bpm) {
    const uint16_t midCount = max((uint16_t)3, (uint16_t)(NUM_LEDS / 6));
    const uint16_t midStart = (uint16_t)(NUM_LEDS / 2 - midCount / 2);
    const uint16_t midEnd   = midStart + midCount;

    if (elapsed < 3000) {
        uint8_t phase = (uint8_t)((uint32_t)elapsed * 3 * 256 / 3000);
        fill_solid(leds, NUM_LEDS, CRGB(triwave8(phase), 0, 0));

    } else if (elapsed < 4600) {
        uint32_t t        = elapsed - 3000;
        uint16_t litCount = (uint16_t)(midCount +
                            (uint32_t)(NUM_LEDS - midCount) * (1600 - t) / 1600);
        uint16_t startIdx = (uint16_t)(NUM_LEDS / 2 - litCount / 2);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        for (uint16_t i = startIdx; i < startIdx + litCount && i < NUM_LEDS; i++)
            leds[i] = CRGB(200, 0, 0);

    } else if (elapsed < 7600) {
        static const CRGB stops[4] = {
            CRGB(220,   0,   0),
            CRGB(220,  90,   0),
            CRGB(220, 180,   0),
            CRGB(200,   0, 180),
        };
        uint32_t t      = elapsed - 4600;
        uint8_t  seg    = (uint8_t)(t / 1000); if (seg > 2) seg = 2;
        uint8_t  mixAmt = (uint8_t)((t - (uint32_t)seg * 1000) * 255 / 1000);
        CRGB     color  = blend(stops[seg], stops[seg + 1], mixAmt);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        for (uint16_t i = midStart; i < midEnd; i++) leds[i] = color;

    } else if (elapsed < 10600) {
        nscale8_video(leds, NUM_LEDS, 200);
        uint8_t sparks = max((uint8_t)2, (uint8_t)(NUM_LEDS / 7));
        for (uint8_t s = 0; s < sparks; s++) leds[random8(NUM_LEDS)] = CRGB(120, 0, 120);

    } else if (elapsed < 13600) {
        uint32_t t    = elapsed - 10600;
        uint32_t tZap = t % 300;
        float    prog = (float)tZap / 299.0f;
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        int16_t lh = (int16_t)((float)midStart * (1.0f - prog));
        if (lh   >= 0 && lh   < (int16_t)NUM_LEDS) leds[lh  ] = CRGB(240, 0, 240);
        if (lh+1 >= 0 && lh+1 < (int16_t)NUM_LEDS) leds[lh+1] = CRGB(130, 0, 130);
        if (lh+2 >= 0 && lh+2 < (int16_t)NUM_LEDS) leds[lh+2] = CRGB( 55, 0,  55);
        int16_t rh = (int16_t)((float)(midEnd - 1) + (float)(NUM_LEDS - midEnd) * prog);
        if (rh   >= 0 && rh   < (int16_t)NUM_LEDS) leds[rh  ] = CRGB(240, 0, 240);
        if (rh-1 >= 0 && rh-1 < (int16_t)NUM_LEDS) leds[rh-1] = CRGB(130, 0, 130);
        if (rh-2 >= 0 && rh-2 < (int16_t)NUM_LEDS) leds[rh-2] = CRGB( 55, 0,  55);

    } else if (elapsed < 14600) {
        uint8_t bri = (uint8_t)((elapsed - 13600) * 220 / 1000);
        fill_solid(leds, NUM_LEDS, CRGB(bri, 0, 0));

    } else if (elapsed < 19600) {
        uint32_t period   = (bpm > 0) ? (60000UL / (uint32_t)bpm) : 857;
        uint32_t tInCycle = (elapsed - 14600) % period;
        uint32_t rem      = period - tInCycle;
        uint8_t  decay    = (uint8_t)((uint32_t)195 * rem * rem / ((uint32_t)period * period));
        fill_solid(leds, NUM_LEDS, CRGB(qadd8(decay, 25), 0, 0));

    } else if (elapsed < 24600) {
        uint32_t t   = elapsed - 19600;
        uint8_t  bri = (t < 5000) ? (uint8_t)(220UL * (5000 - t) / 5000) : 0;
        fill_solid(leds, NUM_LEDS, CRGB(bri, 0, 0));

    } else {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
    }
}

void triggerSyncAnimation() {
    syncStartMs          = millis();
    syncActive           = true;
    hA.gatherPulseActive = false;
    hB.gatherPulseActive = false;
}

void cancelSyncAnimation() {
    syncActive        = false;
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    hA.animPhase      = PHASE_IDLE;
    hB.animPhase      = PHASE_IDLE;
    hA.bgLevel        = 0;
    hB.bgLevel        = 0;
    hA.smoothLitLEDs  = 0.0f;
    hB.smoothLitLEDs  = 0.0f;
}

bool isSyncAnimActive() { return syncActive; }

void showLeds()  { FastLED.show(); }
void clearLeds() { FastLED.clear(); }
