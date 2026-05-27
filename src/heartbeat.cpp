#include "heartbeat.h"
#include <Arduino.h>
#include <PulseSensorPlayground.h>

// Pulse sensor input pin on the ESP32.
static const int PulseWire = D3;

// Threshold for the PulseSensor library to detect a beat.
// This is tuned for the ESP32 12-bit ADC range (0-4095).
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

// Minimum and maximum accepted beat interval.
// This prevents fast false beats from showing as high BPM.
static const int MIN_ACCEPTABLE_IBI_MS = 500;   // 120 BPM max
static const int MAX_ACCEPTABLE_IBI_MS = 1500;  // 40 BPM min

void setupHeartbeat() {
    // Start serial output for debugging and monitoring.
    Serial.begin(115200);
    delay(1000);

    // Set ESP32 ADC resolution to 12 bits.
    analogReadResolution(12);

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

uint8_t heartbeatBrightness() {
    // Default pulse brightness when no heartbeat is detected.
    uint8_t pulse = 180;

    // Get the current timestamp and raw sample for diagnostics.
    unsigned long now = millis();
    int rawSample = pulseSensor.getLatestSample();

    // Heartbeat state values from the PulseSensor library.
    bool justBeat = pulseSensor.sawStartOfBeat();
    bool insideBeat = pulseSensor.isInsideBeat();
    int myBPM = pulseSensor.getBeatsPerMinute();
    int interBeatInterval = pulseSensor.getInterBeatIntervalMs();
    bool bpmReasonable = (interBeatInterval >= 428 && interBeatInterval <= 1500);

    // Print raw sensor diagnostics periodically.
    bool contactGood = (rawSample > 1200 && rawSample < 2900);

    if (now - lastRawPrint >= RAW_PRINT_INTERVAL_MS) {
        Serial.print("CONTACT: ");
        Serial.print(contactGood ? COLOR_GREEN : COLOR_RED);
        Serial.print(contactGood ? "GOOD" : "POOR");
        Serial.print(COLOR_RESET);
        Serial.print(" | RAW: ");
        Serial.print(rawSample);
        Serial.print(" | IBI: ");
        Serial.print(interBeatInterval);
        Serial.print(" ms");
        Serial.print(" | BPM: ");
        if (bpmReasonable) {
            Serial.print(myBPM);
        } else {
            Serial.print("---");
        }
        Serial.println();
        lastRawPrint = now;
    }

    // If the library signals the start of a beat, only accept it when the interval is reasonable.
    if (justBeat) {
        if (bpmReasonable) {
            Serial.println("♥  A HeartBeat Happened !");
            Serial.print("BPM: ");
            Serial.println(myBPM);
            pulse = beatsin8(30, 180, 255);
        } else {
            Serial.println("⚠️  Beat ignored: interval out of range");
            Serial.print("IBI: ");
            Serial.print(interBeatInterval);
            Serial.println(" ms");
        }
    }

    return pulse;
}

void applyHeartbeatPulse(uint8_t pulse) {
    // Scale all LEDs by the current pulse brightness.
    nscale8_video(leds, NUM_LEDS, pulse);
}
