# Syncopation Station — Project Notes

## Overview

Interactive art installation. Participants place fingers on pulse sensors; LEDs respond to heartbeat in real time. MVP: one sensor active. Second sensor stubbed, ready for two-person sync mode.

Hardware: Seeed Studio XIAO ESP32S3 + WS2812B LED strip (GRB, 3.3 V logic).
Firmware: PlatformIO / Arduino framework. Libraries: FastLED, PulseSensor Playground.


---

## LED State Machine

Visual feedback is driven by a 6-phase state machine in `src/led_controller.cpp`.

### IDLE
Low red scanner: 5-LED pulse bouncing left/right. Centre LED is brightest (brightness profile: 12, 45, 110, 45, 12). Runs at 30 bpm sweep via `beatsin8`.

### POSSIBLE
Triggered when 2 consecutive raw ADC samples drop near zero — the earliest detectable hint of a finger. Scanner "lands" and holds as 3 steady red LEDs. Background brightness fades toward black.

### GATHER
Triggered once contact is confirmed (held long enough to trust). LED count climbs one per confirmed beat starting from the initial 3 (= `NUM_LEDS / 6`). So: 3 → 4 → 5 → 6 … The leading (outermost) LED blinks at ~300 ms to signal "waiting for next beat." When a stable BPM is locked (full beat history collected), `smoothLitLEDs` rises quickly to fill the entire strip.

### HOLD
All LEDs solid red at brightness 200. Brief pause (`CONNECTING_HOLD_MS = 500 ms`) before switching to pulse mode.

### PULSE
Beat-locked heartbeat animation. On each beat, all LEDs snap bright (red 245) and decay quadratically to a dim glow (red 25) over the inter-beat interval. Period is smoothed with an 80/20 EMA so tempo changes feel gradual.

**Degradation:** if beats become overdue the strip shrinks from the right — `connectionLevel` decays from 1.0 → 0.0 between 1.5× and 4× the expected interval. Always floors at 1 LED so something is always visible. A new beat snaps everything back to full immediately.

If the beat history expires (8 s with no beat), falls back to GATHER so the user can rebuild.

### DRAIN
Contact lost. Strip wipes right-to-left to black over `DRAIN_MS = 800 ms`. If contact is re-confirmed mid-drain, jumps straight back to PULSE.

---

## Key Constants (`include/led_controller.h`)

| Constant | Default | Notes |
|---|---|---|
| `NUM_LEDS` | 20 (test) / 60 (production) | Change this only — everything else scales |
| `BRIGHTNESS` | 100 | 0–255 global FastLED brightness |
| `MAX_MILLIAMPS` | `NUM_LEDS × 25` | Auto-scales with strip length |
| `GATHER_LEDS_PER_BEAT` | `NUM_LEDS / 6` | Starting floor + beats-per-LED in GATHER |
| `CONNECTING_FILL_MS` | 2500 | Rise speed from gather floor to full strip |
| `CONNECTING_HOLD_MS` | 500 | Hold duration before PULSE |
| `DRAIN_MS` | 800 | Duration of right-to-left wipe on contact loss |

For 30 LEDs/sensor: `GATHER_LEDS_PER_BEAT` = 5, `MAX_MILLIAMPS` = 750.

---

## Contact Detection (`src/heartbeat.cpp`)

Three tiers of contact quality fed into `drawFrame`:

| Signal | Source | Meaning |
|---|---|---|
| `isPossibleContact()` | 2 consecutive raw samples < 50 | Earliest finger hint; holds 4 s |
| `getContactGood()` | Rolling variance + EMA deviation | Sustained signal quality |
| `isContactConfirmed()` | `getContactGood()` held for `CONTACT_CONFIRM_MS` | Safe to trust for visuals |

`getValidBeatCount()` — beats collected since last contact loss (drives GATHER fill level).
`getStableBPM()` — non-zero only when full beat history is collected.
`heartbeatBrightness()` — returns a decaying value 220→0 on each confirmed beat.

**Calibration:** press `c` in the serial monitor to run `runCalibration()`. Samples noise floor then three finger-on rounds, prints recommended `CONTACT_MIN_RANGE`.

---

## Touch Feedback Design (for physical UI)

The LED states are the primary feedback channel — the user cannot see the serial monitor.

| What the user sees | What it means |
|---|---|
| Red scanner bouncing | No finger detected |
| 3 steady LEDs + blinking tip | Finger detected, confirming contact |
| LEDs climbing one at a time, tip blinking | Contact confirmed, collecting beats |
| All LEDs filling rapidly | Stable BPM locked |
| Full strip pulsing to heartbeat | Synced — heartbeat detected |
| Strip shrinking from right | Beats stopping — adjust finger pressure/position |
| Strip wiping to black | Contact lost |

**Training goal:** the shrinking/drain animation gives real-time pressure/position feedback without any screen. User learns optimal placement by watching the strip recover or degrade.

---

## Hardware Notes

- 300–470 Ω series resistor on LED data line (prevents ringing).
- Power WS2812B directly from USB-C 5 V rail (not through the XIAO).
- `setMaxPowerInVoltsAndMilliamps(5, NUM_LEDS * 25)` — FastLED enforces this cap at runtime.
- ADC: 12-bit (0–4095), `analogReadResolution(12)` set in `setupHeartbeat()`.

---

## Two-Sensor Mode (stubbed, not yet wired)

- `updateSyncState(confirmed, false)` — second arg is Person B's confirmed state.
- `drawSyncBloom(isSyncPossible())` — fires when both sensors are simultaneously confirmed.
- `spawnPulse` / `drawConnectionPulse` — traveling blob across the strip, one per person's beat.
- `sync.h` / `connection_pulse.h` — already compiled in, waiting for second sensor wiring.

---

## Git Workflow

```
# Start of session
git pull

# Commit often — before experiments, after wins, before switching machines
git add .
git commit -m "Describe_what_changed"

# End of day
git push
```

---

## To Do

- [ ] Test with external power supply
- [ ] Add physical finger-cutout enclosure
- [ ] Wire second sensor (Person B) — duplicate heartbeat functions, re-enable sync visuals
- [ ] Non-screen feedback for bad touch (audio? haptic? LED pattern?) — see Touch Feedback section above
- [ ] Test with 60-LED strip in production
