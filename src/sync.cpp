#include "sync.h"

// Timestamp when both participants first achieved simultaneous confirmed contact.
// 0 means sync timing has not started — either no one is connected, or the timer was reset.
// Mirrors contactStartMs in heartbeat.cpp: 0 == "not yet started", non-zero == "timing in progress".
static unsigned long syncStartMs = 0;

// Tracks whether both were confirmed in the previous frame.
// Used to detect the rising edge (moment both first connect simultaneously).
static bool prevBothConfirmed = false;

// Debug: print sync progress periodically.
static const unsigned long SYNC_PRINT_INTERVAL_MS = 2000;
static unsigned long lastSyncPrint = 0;

void updateSyncState(bool confirmedA, bool confirmedB) {
    bool bothConfirmed = confirmedA && confirmedB;
    unsigned long now = millis();

    if (bothConfirmed) {
        if (!prevBothConfirmed) {
            // Rising edge — both participants just achieved simultaneous confirmed contact.
            // Start the sync timer. Both must hold for DELAY_BEFORE_POSSIBLE_SYNC_SECONDS.
            syncStartMs = now;
        }
    } else {
        if (syncStartMs != 0) {
            // Either participant broke contact — reset the sync timer entirely.
            // Partial holds don't count: the full duration must be uninterrupted.
            // This mirrors the contact confirmation pattern: earned state requires sustained intent.
            syncStartMs = 0;
        }
    }

    prevBothConfirmed = bothConfirmed;

    // Periodic serial output for debugging sync progress.
    if (bothConfirmed && syncStartMs != 0 && now - lastSyncPrint >= SYNC_PRINT_INTERVAL_MS) {
        float progress = getSyncProgress();
        unsigned long elapsed = (now - syncStartMs) / 1000UL;
        Serial.print("SYNC: ");
        Serial.print((int)(progress * 100));
        Serial.print("% | ");
        Serial.print(elapsed);
        Serial.print("s / ");
        Serial.print(DELAY_BEFORE_POSSIBLE_SYNC_SECONDS);
        Serial.println("s");
        lastSyncPrint = now;
    }
}

bool isSyncPossible() {
    if (syncStartMs == 0) return false;
    return (millis() - syncStartMs) >= (DELAY_BEFORE_POSSIBLE_SYNC_SECONDS * 1000UL);
}

float getSyncProgress() {
    if (syncStartMs == 0) return 0.0f;
    unsigned long elapsed = millis() - syncStartMs;
    unsigned long total = DELAY_BEFORE_POSSIBLE_SYNC_SECONDS * 1000UL;
    if (elapsed >= total) return 1.0f;
    return (float)elapsed / (float)total;
}
