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
    PHASE_IDLE,      // purple breathing — no contact
    PHASE_POSSIBLE,  // 1 blinking LED — raw near-zero detected, awaiting confirmation
    PHASE_GATHER,    // 1 LED per beat + big fill when stable BPM found
    PHASE_HOLD,      // all LEDs solid — brief hold before pulsing
    PHASE_PULSE,     // beat-locked heartbeat pulsing, degrades when beats stop
    PHASE_DRAIN      // right-to-left wipe — contact lost during pulse/hold
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

// ── Helpers ───────────────────────────────────────────────────────────────────

// Render `litCount` red LEDs with a gradient (dim at index 0, bright at leading edge).
// blinkLeading: leading LED flashes on/off at ~300 ms to signal "waiting for next beat".
static void renderGatherLEDs(uint16_t litCount, bool blinkLeading = false) {
    bool leadOn = !blinkLeading || ((millis() / 300) % 2 == 0);
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        if (i < litCount) {
            if (i == litCount - 1 && !leadOn) {
                leds[i] = CRGB::Black;
            } else {
                uint8_t bright = (litCount > 1)
                    ? (uint8_t)map(i, 0, litCount - 1, 40, 180)
                    : 140;
                if (i == litCount - 1) bright = 220; // leading-edge pop
                leds[i] = CRGB(bright, 0, 0);
            }
        } else {
            leds[i] = CRGB::Black;
        }
    }
}

// ── Main frame function ───────────────────────────────────────────────────────

void drawFrame(bool possibleContact, bool contact, bool confirmed,
               int beatCount, uint8_t bpm, uint8_t beatPulse) {

    unsigned long now = millis();

    // ── Smooth LED fill update ────────────────────────────────────────────────
    // Target: 1 LED per beat while gathering; jumps to full strip when stable BPM found.
    const float riseSpeed = (float)NUM_LEDS * 10.0f / CONNECTING_FILL_MS;
    float targetLit = 0.0f;
    if (animPhase == PHASE_GATHER) {
        // Floor at GATHER_LEDS_PER_BEAT; each beat adds 1 LED so progression is 3→4→5→6…
        float beatTarget = max((float)GATHER_LEDS_PER_BEAT,
                               min((float)NUM_LEDS, (float)GATHER_LEDS_PER_BEAT + (float)beatCount));
        targetLit = (bpm > 0) ? (float)NUM_LEDS : beatTarget;
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
                animPhase     = PHASE_GATHER;
                bgLevel       = 0;
                smoothLitLEDs = 1.0f; // start at 1 LED → rises to 3 before first beat
            } else if (possibleContact) {
                animPhase = PHASE_POSSIBLE;
            }
            break;

        case PHASE_POSSIBLE:
            if (bgLevel > 0) bgLevel = (bgLevel > 1) ? bgLevel - 1 : 0;
            if (confirmed) {
                animPhase     = PHASE_GATHER;
                bgLevel       = 0;
                smoothLitLEDs = 1.0f; // start at 1 LED → rises to 3 before first beat
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
                smoothLitLEDs = max(1.0f, connectionLevel * (float)NUM_LEDS);
                animPhase     = PHASE_GATHER;
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
            renderGatherLEDs((uint16_t)smoothLitLEDs, true);
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
    }
}

void showLeds() { FastLED.show(); }
void clearLeds() { FastLED.clear(); }
