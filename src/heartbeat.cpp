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
static const char* COLOR_RESET = "\x1b[0m";
static const char* COLOR_RED = "\x1b[31m";
static const char* COLOR_GREEN = "\x1b[32m";
static const char* COLOR_YELLOW = "\x1b[33m";

// Library object used to interact with PulseSensorPlayground.
static PulseSensorPlayground pulseSensor;

// Print status every 500ms for easier readability.
static const unsigned long RAW_PRINT_INTERVAL_MS = 500;
static unsigned long lastRawPrint = 0;

// Rolling signal window for contact quality — checks how much the signal swings over ~800ms.
// A floating/idle sensor sits flat; a real heartbeat produces visible variation.
static const int CONTACT_WINDOW = 40;       // 40 samples × 20ms loop = 800ms
static const int CONTACT_MIN_RANGE = 200;  // ADC counts of swing required to count as contact
static int contactSamples[CONTACT_WINDOW];
static int contactIdx = 0;
static bool contactWindowFull = false;

// Minimum and maximum accepted beat interval.
// This prevents fast false beats from showing as high BPM.
static const int MIN_ACCEPTABLE_IBI_MS = 428;   // 140 BPM max // 500 is 120 BP Max
static const int MAX_ACCEPTABLE_IBI_MS = 1500;  // 40 BPM min

// Rolling BPM average — requires this many consecutive valid beats before
// reporting a stable BPM. Raise to be more conservative, lower to respond faster.
static const int BPM_HISTORY_SIZE = 6;  // Number of valid beats to average for stable BPM reading. Adjust to taste. 5 is a good balance for responsiveness while filtering outliers. 10 can be for live.
static int bpmHistory[BPM_HISTORY_SIZE] = {0};
static int bpmHistoryIndex = 0;
static int bpmHistoryCount = 0;

// If no valid beat arrives within this window, reset the average (contact lost).
static const unsigned long BPM_RESET_TIMEOUT_MS = 5000;
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
    pulseSensor.blinkOnPulse(PulseLed);
    pulseSensor.setThreshold(Threshold);

    // Initialize the library and confirm startup.
    if (pulseSensor.begin()) {
        Serial.println("We created a pulseSensor Object !");
    } else {
        Serial.println("PulseSensor failed to start.");
    }
}

bool getContactGood() {
    return lastContactGood || (millis() - lastContactGoodTime < CONTACT_HOLD_MS);
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
    bool contactGood = (rawSample > 1200 && rawSample < 2900) && (cMax - cMin >= CONTACT_MIN_RANGE);
    
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
        Serial.print(" | RAW: ");
        Serial.print(rawSample);
        Serial.print(" | IBI: "); // Inter-beat interval
        Serial.print(interBeatInterval);
        Serial.print("ms");
        Serial.print(" | BPM: ");
        if (bpmReasonable) {
            Serial.print(myBPM);
        } else {
            Serial.print("---");
        }
        Serial.println();
        lastRawPrint = now;
    }

    // Only consider the beat if the inter-beat interval is within a reasonable range.
    if (justBeat && isContactConfirmed()) {
        if (bpmReasonable) {
            bpmHistory[bpmHistoryIndex] = myBPM; // Add to rolling BPM history for averaging
            bpmHistoryIndex = (bpmHistoryIndex + 1) % BPM_HISTORY_SIZE; 
            if (bpmHistoryCount < BPM_HISTORY_SIZE) bpmHistoryCount++; // Track how many valid beats we've seen for stable BPM calculation
            lastValidBeatTime = now;
            justBeatFlag = true;

            int stable = getStableBPM(); // Get the current stable BPM average for diagnostics
            // Beat detected — print in red for visibility
            Serial.print(COLOR_RED); 
            Serial.print("♥  Beat detected!");
            Serial.print(COLOR_RESET);
            Serial.print(" | RAW: "); // Diagnostics for raw signal on beat
            Serial.print(rawSample);
            Serial.print(" | BPM: ");
            Serial.print(myBPM);
            // If we have a stable BPM average, print it; otherwise indicate we're still warming up.
            if (stable > 0) {
                Serial.print(" | ♥ Stable BPM: ");
                Serial.print(stable);
            }
            // Still warming up — not enough valid beats for stable BPM average. 
            else { 
                Serial.print(" | Warming up (");
                Serial.print(bpmHistoryCount);
                Serial.print("/");
                Serial.print(BPM_HISTORY_SIZE);
                Serial.print(")");
            }
            Serial.println();
            // Start the beat pulse at a bright value; it will decay in heartbeatBrightness() and applyHeartbeatPulse().
            lastBeatMs = now;
            // Start the beat pulse at a bright value; it will decay in heartbeatBrightness() and applyHeartbeatPulse().
            beatPeak = 220; 
        } else {
            Serial.print("⚠️  Beat ignored | IBI: ");
            Serial.print(interBeatInterval);
            Serial.println("ms");
        }
    }

    // No confirmed contact:
    // Reset heartbeat state so old BPM averages, pulse brightness, or beat flags do not linger
    // after the finger is removed.
    if (!isContactConfirmed()) {
        bpmHistoryCount = 0;
        beatPeak = 0;
        justBeatFlag = false;
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
