#include "led_controller.h"

// LED data array
CRGB leds[NUM_LEDS];

void setupLedController() {

    // Initialize LED strip
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);

    // Limit total power draw
    FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);

    // Global brightness (0–255)
    FastLED.setBrightness(BRIGHTNESS);

    // Start with LEDs off
    FastLED.clear();
    FastLED.show();
}

// Two-phase transition: purple fades to black first, then red fades in. Reverses on release.
// This ensures we never jump directly between purple and red.
static uint8_t bgLevel  = 255;  // 255 = full purple, 0 = black
static uint8_t redLevel = 0;    // 0 = black, 255 = full red; used for idle-state fade-out only

// Thermometer fill animation phases.
enum AnimPhase : uint8_t { PHASE_IDLE, PHASE_FILL, PHASE_HOLD, PHASE_PULSE };
static AnimPhase     animPhase        = PHASE_IDLE;
static unsigned long animPhaseStartMs = 0;

// Beat-locked pulse state — updated each time a beat fires in PHASE_PULSE.
// Using smoothed IBI keeps the visual from jumping when BPM estimates shift.
static uint8_t       prevBeatPulse = 0;
static uint32_t      animPeriodMs  = 857;   // smoothed inter-beat interval, ~70 BPM
static unsigned long animBeatMs    = 0;

void drawFrame(bool contact, bool confirmed, bool connecting, uint8_t bpm, uint8_t beatPulse) {
    // ── State transitions ─────────────────────────────────────────────────────
    if (!confirmed) {
        // Reset animation on contact loss; leave redLevel intact so it fades out naturally.
        animPhase        = PHASE_IDLE;
        animPhaseStartMs = 0;
        animBeatMs       = 0;
        prevBeatPulse    = 0;

        if (contact) {
            // Contact detected but not yet confirmed — slowly dim purple over the confirmation window.
            bgLevel = (bgLevel > 1) ? bgLevel - 1 : 0;
        } else {
            // No contact: fade red out first, then bring purple back.
            if (redLevel > 0)  redLevel = (redLevel > 4) ? redLevel - 4 : 0;
            else               bgLevel  = (bgLevel  < 252) ? bgLevel  + 5 : 255;
        }
    } else {
        // Confirmed: drain purple first.
        if (bgLevel > 0) bgLevel = (bgLevel > 2) ? bgLevel - 2 : 0;

        unsigned long now = millis();

        // Rising edge: start fill once purple is gone and enough beats are gathered.
        if (connecting && animPhase == PHASE_IDLE && bgLevel == 0) {
            animPhase        = PHASE_FILL;
            animPhaseStartMs = now;
        }

        // Advance fill → hold → pulse.
        if (animPhase == PHASE_FILL && now - animPhaseStartMs >= CONNECTING_FILL_MS) {
            animPhase        = PHASE_HOLD;
            animPhaseStartMs = now;
        }
        if (animPhase == PHASE_HOLD && now - animPhaseStartMs >= CONNECTING_HOLD_MS) {
            animPhase   = PHASE_PULSE;
            redLevel    = 255;   // restore so idle fade-out is smooth if contact is later lost
            animBeatMs  = now;   // start at full brightness on entry
        }
    }

    // ── Rendering ─────────────────────────────────────────────────────────────
    if (animPhase == PHASE_FILL) {
        unsigned long elapsed = millis() - animPhaseStartMs;
        // Single-sensor: fill full strip. Two sensors: change fillTarget to NUM_LEDS / 2.
        uint16_t fillTarget = NUM_LEDS;
        uint16_t fillCount  = (uint16_t)((elapsed * fillTarget) / CONNECTING_FILL_MS);
        if (fillCount > fillTarget) fillCount = fillTarget;

        for (uint16_t i = 0; i < NUM_LEDS; i++) {
            if (i < fillCount) {
                // Linear gradient dim→bright across the filled portion; leading edge pops brightest.
                uint8_t bright = (fillTarget > 1)
                    ? (uint8_t)map(i, 0, fillTarget - 1, 40, 180)
                    : 180;
                if (i == fillCount - 1) bright = 220;
                leds[i] = CRGB(bright, 0, 0);
            } else {
                leds[i] = CRGB::Black;
            }
        }
    } else if (animPhase == PHASE_HOLD) {
        fill_solid(leds, NUM_LEDS, CRGB(200, 0, 0));

    } else if (animPhase == PHASE_PULSE) {
        // Beat-locked smooth pulsing.
        // Detect beat rising edge: beatPulse jumps from ~0 to ~213 when a beat fires.
        // beatsin8 is intentionally NOT used here — any BPM change shifts its phase
        // instantly, causing visible stutter. This animation is locked to actual beats.
        bool beatJustFired = (beatPulse > 100) && (prevBeatPulse < 50);
        prevBeatPulse = beatPulse;

        if (beatJustFired && bpm > 0) {
            animBeatMs = millis();
            uint32_t target = 60000UL / (uint32_t)bpm;
            // 80/20 EMA — period drifts slowly toward actual IBI, never jumps.
            animPeriodMs = (animPeriodMs * 4 + target) / 5;
        }

        uint32_t elapsed   = (uint32_t)(millis() - animBeatMs);
        if (elapsed > animPeriodMs) elapsed = animPeriodMs;
        uint32_t remaining = animPeriodMs - elapsed;
        // Quadratic ease-out: bright at beat, smooth decay, ambient glow between beats.
        uint8_t decay    = (uint8_t)((uint32_t)220 * remaining * remaining /
                                      ((uint32_t)animPeriodMs * animPeriodMs));
        uint8_t finalRed = qadd8(decay, 25);
        fill_solid(leds, NUM_LEDS, CRGB(finalRed, 0, 0));

    } else {
        // PHASE_IDLE: bgLevel/redLevel driven (purple ↔ black ↔ fading-out red).
        uint8_t pulseBPM     = (bpm > 0) ? bpm : 60;
        uint8_t purpleBright = scale8(beatsin8(6, 5, 80), bgLevel);
        uint8_t baseRed      = scale8(beatsin8(pulseBPM, 0, 220), redLevel);
        uint8_t finalRed     = qadd8(baseRed, beatPulse);
        if (redLevel > 0) {
            fill_solid(leds, NUM_LEDS, CRGB(finalRed, 0, 0));
        } else {
            fill_solid(leds, NUM_LEDS, CHSV(200, 180, purpleBright));
        }
    }
}

// Send data to LEDs
void showLeds() {
    FastLED.show();
}

// Clear LED buffer
void clearLeds() {
    FastLED.clear();
}
