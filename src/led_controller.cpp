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

// ── State ─────────────────────────────────────────────────────────────────────

// Purple background level: 255 = full glow, 0 = black.
static uint8_t bgLevel = 255;

enum AnimPhase : uint8_t {
    PHASE_IDLE,      // red scanner — no contact
    PHASE_POSSIBLE,  // 3 steady LEDs — raw near-zero detected, awaiting confirmation
    PHASE_GATHER,    // beat-driven fill + pulse animation
    PHASE_HOLD,      // all LEDs solid — brief hold before pulsing
    PHASE_PULSE,     // beat-locked heartbeat pulsing, degrades when beats stop
    PHASE_DRAIN,     // right-to-left wipe — contact lost during pulse/hold
    PHASE_SYNC       // 15-second synchronized animation sequence
};
static AnimPhase     animPhase        = PHASE_IDLE;
static unsigned long animPhaseStartMs = 0;

// GATHER: smooth float so LEDs ease in rather than snapping.
static float smoothLitLEDs = 0.0f;

// PULSE: beat-locked animation state.
static uint8_t       prevBeatPulse = 0;
static uint32_t      animPeriodMs  = 857;   // smoothed inter-beat interval (~70 BPM default)
static unsigned long animBeatMs    = 0;

// DRAIN: how many LEDs were lit when the drain started (can be <NUM_LEDS if degraded).
static uint16_t drainLitAtStart = NUM_LEDS;

// SYNC: start timestamp for the 15-second animation.
static unsigned long syncStartMs = 0;

// GATHER: per-beat 3-LED traveling pulse.
static int   prevBeatCount     = 0;
static float gatherFillLevel   = 0.0f;  // committed fill; stays put while pulse travels
static float gatherPulsePos    = 0.0f;  // traveling pulse head (fractional LED index)
static float gatherPulseEnd    = 0.0f;  // pulse destination
static bool  gatherPulseActive = false;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Render `litCount` red LEDs with a gradient (dim at index 0, bright at leading edge).
// blinkLeading: last 3 positions animate as a bouncing VU meter (0→3→0 sine) while
// the rest of the fill holds solid — signals "waiting for next beat".
static void renderGatherLEDs(uint16_t litCount, bool blinkLeading = false) {
    uint16_t solidCount = (blinkLeading && litCount >= 3) ? litCount - 3 : litCount;
    uint8_t  meterLit   = blinkLeading ? beatsin8(70, 0, 3) : 0;

    static const uint8_t mbri[3] = { 100, 160, 220 };

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        if (i < solidCount) {
            uint8_t bright = (litCount > 1)
                ? (uint8_t)map(i, 0, litCount - 1, 40, 180)
                : 140;
            leds[i] = CRGB(bright, 0, 0);
        } else if (i < solidCount + meterLit) {
            leds[i] = CRGB(mbri[i - solidCount], 0, 0);
        } else {
            leds[i] = CRGB::Black;
        }
    }
}

static void renderSyncAnimation(uint32_t elapsed, uint8_t bpm);  // defined below drawFrame

// ── Main frame function ───────────────────────────────────────────────────────

void drawFrame(bool possibleContact, bool contact, bool confirmed,
               int beatCount, uint8_t bpm, uint8_t beatPulse) {

    unsigned long now = millis();

    // ── Smooth LED fill update ────────────────────────────────────────────────
    // Target: 1 LED per beat while gathering; jumps to full strip when stable BPM found.
    const float riseSpeed = (float)NUM_LEDS * 10.0f / CONNECTING_FILL_MS;
    float targetLit = 0.0f;
    if (animPhase == PHASE_GATHER) {
        if (bpm > 0) {
            targetLit = (float)NUM_LEDS;  // locks in for HOLD transition
        } else {
            smoothLitLEDs = gatherFillLevel;  // pulse system owns movement; keep smoothLitLEDs in sync
            targetLit     = gatherFillLevel;
        }
    } else if (animPhase == PHASE_HOLD || animPhase == PHASE_PULSE) {
        targetLit = (float)NUM_LEDS;
    }
    if (smoothLitLEDs < targetLit) smoothLitLEDs = min(smoothLitLEDs + riseSpeed, targetLit);
    else                           smoothLitLEDs = max(smoothLitLEDs - riseSpeed, targetLit);

    // ── Beat detection — must happen before connectionLevel is computed ────────
    // beatPulse spikes from ~0 to ~213 on each confirmed beat.
    bool beatJustFired = (animPhase == PHASE_PULSE) && (beatPulse > 100) && (prevBeatPulse < 50);
    prevBeatPulse = beatPulse;
    if (beatJustFired && bpm > 0) {
        animBeatMs = now;
        uint32_t target = 60000UL / (uint32_t)bpm;
        animPeriodMs = (animPeriodMs * 4 + target) / 5; // 80/20 EMA — drifts, never jumps
    }

    // ── GATHER: beat detection → spawn 3-LED traveling pulse ─────────────────
    bool gatherBeatFired = (animPhase == PHASE_GATHER) && (bpm == 0) && (beatCount > prevBeatCount);
    prevBeatCount = beatCount;
    if (gatherBeatFired) {
        gatherFillLevel   = gatherPulseEnd;   // commit previous destination to solid fill
        gatherPulsePos    = gatherFillLevel;
        gatherPulseEnd    = min((float)NUM_LEDS,
                                (float)beatCount / (float)GATHER_BEAT_TOTAL * (float)NUM_LEDS);
        gatherPulseActive = true;
    }
    if (animPhase == PHASE_GATHER && gatherPulseActive) {
        gatherPulsePos = min(gatherPulsePos + 0.5f, gatherPulseEnd);
        if (gatherPulsePos >= gatherPulseEnd) {
            gatherFillLevel   = gatherPulseEnd;
            gatherPulseActive = false;
        }
    }

    // ── Connection level (PULSE only) ─────────────────────────────────────────
    // 1.0 = beats on time, decays toward 0.0 as beats become overdue.
    // Starts degrading after 1.5× expected interval, fully degraded at 4× interval.
    // On a beat, animBeatMs resets → connectionLevel snaps back to 1.0 immediately.
    uint32_t timeSinceLastBeat = (uint32_t)(now - animBeatMs);
    uint32_t degradeStartMs    = animPeriodMs + animPeriodMs / 2; // 1.5×
    uint32_t degradeFullMs     = animPeriodMs * 4;                // 4×
    float connectionLevel = 1.0f;
    if (animPhase == PHASE_PULSE && timeSinceLastBeat > degradeStartMs) {
        connectionLevel = (timeSinceLastBeat >= degradeFullMs) ? 0.0f
            : 1.0f - (float)(timeSinceLastBeat - degradeStartMs)
                    / (float)(degradeFullMs - degradeStartMs);
    }

    // ── Phase transitions ─────────────────────────────────────────────────────
    switch (animPhase) {

        case PHASE_IDLE:
            if (possibleContact || contact) {
                bgLevel = (bgLevel > 1) ? bgLevel - 1 : 0;
            } else {
                bgLevel = (bgLevel < 252) ? bgLevel + 5 : 255;
            }
            if (confirmed) {
                animPhase         = PHASE_GATHER;
                bgLevel           = 0;
                smoothLitLEDs     = (float)GATHER_LEDS_PER_BEAT;
                gatherFillLevel   = (float)GATHER_LEDS_PER_BEAT;
                gatherPulsePos    = (float)GATHER_LEDS_PER_BEAT;
                gatherPulseEnd    = (float)GATHER_LEDS_PER_BEAT;
                gatherPulseActive = false;
                prevBeatCount     = beatCount;
            } else if (possibleContact) {
                animPhase = PHASE_POSSIBLE;
            }
            break;

        case PHASE_POSSIBLE:
            if (bgLevel > 0) bgLevel = (bgLevel > 1) ? bgLevel - 1 : 0;
            if (confirmed) {
                animPhase         = PHASE_GATHER;
                bgLevel           = 0;
                smoothLitLEDs     = (float)GATHER_LEDS_PER_BEAT;
                gatherFillLevel   = (float)GATHER_LEDS_PER_BEAT;
                gatherPulsePos    = (float)GATHER_LEDS_PER_BEAT;
                gatherPulseEnd    = (float)GATHER_LEDS_PER_BEAT;
                gatherPulseActive = false;
                prevBeatCount     = beatCount;
            } else if (!possibleContact && !contact) {
                animPhase = PHASE_IDLE;
            }
            break;

        case PHASE_GATHER:
            if (!confirmed && !contact && !possibleContact) {
                animPhase = PHASE_IDLE;
                bgLevel   = 0;
            } else if (!confirmed && (contact || possibleContact)) {
                animPhase = PHASE_POSSIBLE;
                bgLevel   = 0;
            } else if (bpm > 0 && smoothLitLEDs >= (float)NUM_LEDS - 0.5f) {
                smoothLitLEDs    = (float)NUM_LEDS;
                animPhase        = PHASE_HOLD;
                animPhaseStartMs = now;
            }
            break;

        case PHASE_HOLD:
            if (!confirmed) {
                drainLitAtStart  = NUM_LEDS;
                animPhase        = PHASE_DRAIN;
                animPhaseStartMs = now;
            } else if (now - animPhaseStartMs >= CONNECTING_HOLD_MS) {
                animPhase  = PHASE_PULSE;
                animBeatMs = now; // start at full brightness
            }
            break;

        case PHASE_PULSE:
            if (!confirmed) {
                // Drain from wherever degradation left the strip — not always full.
                drainLitAtStart  = max((uint16_t)1,
                                       (uint16_t)(connectionLevel * NUM_LEDS + 0.5f));
                animPhase        = PHASE_DRAIN;
                animPhaseStartMs = now;
            } else if (bpm == 0) {
                // BPM history expired (8 s without a beat) — drop back to gather.
                // smoothLitLEDs picks up from current degraded position (min 1) so
                // the rebuild animation runs naturally from that point.
                float resumeLevel = max((float)GATHER_LEDS_PER_BEAT,
                                         connectionLevel * (float)NUM_LEDS);
                smoothLitLEDs     = resumeLevel;
                gatherFillLevel   = resumeLevel;
                gatherPulsePos    = resumeLevel;
                gatherPulseEnd    = resumeLevel;
                gatherPulseActive = false;
                prevBeatCount     = beatCount;
                animPhase         = PHASE_GATHER;
            }
            break;

        case PHASE_DRAIN:
            if (confirmed) {
                animPhase  = PHASE_PULSE;
                animBeatMs = now;
            } else if (now - animPhaseStartMs >= DRAIN_MS) {
                animPhase     = PHASE_IDLE;
                bgLevel       = 0;
                smoothLitLEDs = 0.0f;
                prevBeatPulse = 0;
            }
            break;

        case PHASE_SYNC:
            if ((uint32_t)(now - syncStartMs) >= 27600) {
                fill_solid(leds, NUM_LEDS, CRGB::Black);
                animPhase     = PHASE_IDLE;
                bgLevel       = 0;
                smoothLitLEDs = 0.0f;
            }
            break;
    }

    // ── Rendering ─────────────────────────────────────────────────────────────
    switch (animPhase) {

        case PHASE_IDLE: {
            // Low red scanner: 5-LED pulse bouncing back and forth, centre brightest.
            uint8_t scanPos = beatsin8(30, 0, NUM_LEDS - 1);
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            static const int8_t  off[5]   = { -2, -1,  0,  1,  2 };
            static const uint8_t bri[5]   = {  12, 45, 110, 45, 12 };
            for (uint8_t j = 0; j < 5; j++) {
                int16_t idx = (int16_t)scanPos + off[j];
                if (idx >= 0 && idx < (int16_t)NUM_LEDS)
                    leds[idx] = CRGB(bri[j], 0, 0);
            }
            break;
        }

        case PHASE_POSSIBLE: {
            // Scanner lands: show first 3 LEDs steady so the user sees a clear "touch registered".
            renderGatherLEDs(3);
            break;
        }

        case PHASE_GATHER:
            if (bpm > 0) {
                // BPM locked — smooth fill rising to full before HOLD.
                renderGatherLEDs((uint16_t)smoothLitLEDs, false);
            } else {
                // Solid fill up to committed level.
                renderGatherLEDs((uint16_t)gatherFillLevel, !gatherPulseActive);
                // 3-LED traveling pulse: dim → mid → bright at head.
                if (gatherPulseActive) {
                    auto head = (int16_t)gatherPulsePos;
                    static const int8_t  poff[3] = { -2, -1,  0 };
                    static const uint8_t pbri[3] = { 80, 150, 230 };
                    for (uint8_t j = 0; j < 3; j++) {
                        int16_t idx = head + poff[j];
                        if (idx >= 0 && idx < (int16_t)NUM_LEDS)
                            leds[idx] = CRGB(pbri[j], 0, 0);
                    }
                }
            }
            break;

        case PHASE_HOLD:
            fill_solid(leds, NUM_LEDS, CRGB(200, 0, 0));
            break;

        case PHASE_PULSE:
            if (connectionLevel < 1.0f) {
                // Degraded: strip shrinks from right as beats become overdue.
                // Floor at 1 LED — always shows the user something is still alive.
                // A beat firing snaps connectionLevel back to 1.0 immediately.
                uint16_t litCount = max((uint16_t)1,
                                        (uint16_t)(connectionLevel * NUM_LEDS + 0.5f));
                renderGatherLEDs(litCount);
            } else {
                // Full pulsing: quadratic decay from beat peak to ambient glow.
                uint32_t elapsed   = timeSinceLastBeat;
                if (elapsed > animPeriodMs) elapsed = animPeriodMs;
                uint32_t remaining = animPeriodMs - elapsed;
                uint8_t decay    = (uint8_t)((uint32_t)220 * remaining * remaining /
                                              ((uint32_t)animPeriodMs * animPeriodMs));
                uint8_t finalRed = qadd8(decay, 25);
                fill_solid(leds, NUM_LEDS, CRGB(finalRed, 0, 0));
            }
            break;

        case PHASE_DRAIN: {
            // Right-to-left wipe starting from drainLitAtStart (may be < NUM_LEDS if degraded).
            uint32_t elapsed = (uint32_t)(now - animPhaseStartMs);
            uint16_t litCount = (elapsed < DRAIN_MS)
                ? (uint16_t)((uint32_t)drainLitAtStart * (DRAIN_MS - elapsed) / DRAIN_MS)
                : 0;
            for (uint16_t i = 0; i < NUM_LEDS; i++) {
                leds[i] = (i < litCount) ? CRGB(180, 0, 0) : CRGB::Black;
            }
            break;
        }

        case PHASE_SYNC:
            renderSyncAnimation((uint32_t)(now - syncStartMs), bpm);
            break;
    }
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
    const uint16_t midEnd   = midStart + midCount;  // exclusive

    // ── 4 red pulses (0–3000 ms) ──────────────────────────────────────────────
    if (elapsed < 3000) {
        uint8_t phase = (uint8_t)((uint32_t)elapsed * 3 * 256 / 3000);
        fill_solid(leds, NUM_LEDS, CRGB(triwave8(phase), 0, 0));

    // ── Smooth wipe from ends to centre (3000–4600 ms, 1600 ms = 20% faster) ─
    } else if (elapsed < 4600) {
        uint32_t t        = elapsed - 3000;                             // 0–1600 ms
        uint16_t litCount = (uint16_t)(midCount +
                            (uint32_t)(NUM_LEDS - midCount) * (1600 - t) / 1600);
        uint16_t startIdx = (uint16_t)(NUM_LEDS / 2 - litCount / 2);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        for (uint16_t i = startIdx; i < startIdx + litCount && i < NUM_LEDS; i++)
            leds[i] = CRGB(200, 0, 0);

    // ── Centre colour shift: red → orange → yellow → magenta (4600–7600 ms) ──
    } else if (elapsed < 7600) {
        static const CRGB stops[4] = {
            CRGB(220,   0,   0),   // red
            CRGB(220,  90,   0),   // orange
            CRGB(220, 180,   0),   // yellow
            CRGB(200,   0, 180),   // magenta
        };
        uint32_t t      = elapsed - 4600;                               // 0–3000 ms
        uint8_t  seg    = (uint8_t)(t / 1000); if (seg > 2) seg = 2;
        uint8_t  mixAmt = (uint8_t)((t - (uint32_t)seg * 1000) * 255 / 1000);
        CRGB     color  = blend(stops[seg], stops[seg + 1], mixAmt);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        for (uint16_t i = midStart; i < midEnd; i++) leds[i] = color;

    // ── Electrical storm: frantic magenta sparks across full strip (7600–10600 ms)
    // Each frame fades existing pixels and scatters new random sparks everywhere.
    } else if (elapsed < 10600) {
        nscale8_video(leds, NUM_LEDS, 200);                             // fast fade (~200 ms trail)
        uint8_t sparks = max((uint8_t)2, (uint8_t)(NUM_LEDS / 7));
        for (uint8_t s = 0; s < sparks; s++) {
            leds[random8(NUM_LEDS)] = CRGB(120, 0, 120);
        }

    // ── 10 electricity zaps: centre → both ends (10600–13600 ms) ─────────────
    // Two 3-LED pulses launch from the strip centre simultaneously, travel to
    // opposite ends, and repeat every 300 ms — same shape as the IDLE scanner.
    } else if (elapsed < 13600) {
        uint32_t t    = elapsed - 10600;
        uint32_t tZap = t % 300;
        float    prog = (float)tZap / 299.0f;

        fill_solid(leds, NUM_LEDS, CRGB::Black);

        // Left pulse: head starts at midStart, travels to LED 0; trail is behind (higher index).
        int16_t lh = (int16_t)((float)midStart * (1.0f - prog));
        if (lh   >= 0 && lh   < (int16_t)NUM_LEDS) leds[lh  ] = CRGB(240, 0, 240);
        if (lh+1 >= 0 && lh+1 < (int16_t)NUM_LEDS) leds[lh+1] = CRGB(130, 0, 130);
        if (lh+2 >= 0 && lh+2 < (int16_t)NUM_LEDS) leds[lh+2] = CRGB( 55, 0,  55);

        // Right pulse: head starts at midEnd-1, travels to last LED; trail is behind (lower index).
        int16_t rh = (int16_t)((float)(midEnd - 1) + (float)(NUM_LEDS - midEnd) * prog);
        if (rh   >= 0 && rh   < (int16_t)NUM_LEDS) leds[rh  ] = CRGB(240, 0, 240);
        if (rh-1 >= 0 && rh-1 < (int16_t)NUM_LEDS) leds[rh-1] = CRGB(130, 0, 130);
        if (rh-2 >= 0 && rh-2 < (int16_t)NUM_LEDS) leds[rh-2] = CRGB( 55, 0,  55);

    // ── Fade up from black to full red (13600–14600 ms) ──────────────────────
    } else if (elapsed < 14600) {
        uint8_t bri = (uint8_t)((elapsed - 13600) * 220 / 1000);
        fill_solid(leds, NUM_LEDS, CRGB(bri, 0, 0));

    // ── Full-strip BPM pulse (14600–19600 ms) ─────────────────────────────────
    // Quadratic decay per heartbeat cycle, same shape as PHASE_PULSE.
    // Falls back to 70 BPM if no stable reading is available.
    } else if (elapsed < 19600) {
        uint32_t period   = (bpm > 0) ? (60000UL / (uint32_t)bpm) : 857;
        uint32_t tInCycle = (elapsed - 14600) % period;
        uint32_t rem      = period - tInCycle;
        uint8_t  decay    = (uint8_t)((uint32_t)195 * rem * rem / ((uint32_t)period * period));
        fill_solid(leds, NUM_LEDS, CRGB(qadd8(decay, 25), 0, 0));

    // ── Slow fade to black (19600–24600 ms) ───────────────────────────────────
    } else if (elapsed < 24600) {
        uint32_t t   = elapsed - 19600;
        uint8_t  bri = (t < 5000) ? (uint8_t)(220UL * (5000 - t) / 5000) : 0;
        fill_solid(leds, NUM_LEDS, CRGB(bri, 0, 0));

    // ── Silence (24600–27600 ms) ───────────────────────────────────────────────
    } else {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
    }
}

void triggerSyncAnimation() {
    syncStartMs       = millis();
    gatherPulseActive = false;
    animPhase         = PHASE_SYNC;
}

void cancelSyncAnimation() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    animPhase     = PHASE_IDLE;
    bgLevel       = 0;
    smoothLitLEDs = 0.0f;
}

bool isSyncAnimActive() { return animPhase == PHASE_SYNC; }

void showLeds() { FastLED.show(); }
void clearLeds() { FastLED.clear(); }
