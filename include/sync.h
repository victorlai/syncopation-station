#pragma once
#include <Arduino.h>

// How long both participants must maintain confirmed contact before sync can occur.
// This delay is intentional — sync should feel earned, not immediate.
// Longer values create a more meditative, emotionally weighted experience.
// Shorter values feel more game-like and reactive.
static const unsigned long DELAY_BEFORE_POSSIBLE_SYNC_SECONDS = 15;

// Call once per loop with the current confirmed contact state for each participant.
// Handles all timer logic: start, reset, and progress tracking.
void updateSyncState(bool confirmedA, bool confirmedB);

// Returns true once both participants have held confirmed contact for the full delay.
// Both must maintain contact continuously — any drop resets the timer.
bool isSyncPossible();

// Returns progress from 0.0 (not started) to 1.0 (sync reached).
// Use this for gradual visual or audio build-up leading into the sync moment.
float getSyncProgress();
