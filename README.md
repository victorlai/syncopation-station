Syncopation Station

Heartbeat Reactive Art Installation using ESP32-S3, LEDs, Sensors & Sound

---

Overview

Syncopation Station is an interactive art installation exploring connection, rhythm, and co-regulation through light and sound.

Participants place their fingers on heartbeat sensors. Their pulse data is read in real time using an ESP32-S3 microcontroller and translated into reactive LED animations. The installation guides the user through touch, training them to find the right finger placement using only the LEDs as feedback — no screen or instructions needed.

The installation is designed to:

* encourage slowing down and presence
* train participants through visual feedback to find optimal sensor contact
* visualize synchronization between two people
* create an immersive glowing pulse environment
* blend technology with embodied human experience

---

Hardware

Controller

* Seeed Studio XIAO ESP32S3

Sensors

* 2x Pulse Sensor Amped heartbeat sensors (Sensor A → D3, Sensor B → D8)
* Optional VL53L0X / VL53L1X distance sensor

Lighting

* WS2812B LED Strip (60 LEDs in production, 20 for development)
* Diffusion tubing / silicone neon tubing
* 300–470 Ω series resistor on data line (prevents ringing at 3.3 V logic)

Audio (planned)

* Resonance speaker / exciter
* PAM8403 amplifier module

Power

* USB-C power (5V 2A minimum; 5V 3A recommended at BRIGHTNESS > 150 or prolonged SYNC)
* 20,000mAh portable battery bank

Power draw at BRIGHTNESS=100, 60 LEDs:

  State                        Draw
  IDLE (5-LED scanner)         ~75 mA / 0.4 W
  GATHER / PULSE (full red)    ~410 mA / 2.1 W
  SYNC animation (magenta)     ~760 mA / 3.8 W  (FastLED caps at 1500 mA)

Standby runtime (IDLE, no contact — MCU dominates at ~50 mA, LEDs ~7 mA):
  10,000 mAh bank              ~113 hours / 4.7 days
  20,000 mAh bank              ~226 hours / 9.4 days

Active sessions (3 min, ~300 mA average):
  Energy per session           ~75 mWh
  10 sessions (30 min)         ~0.75 Wh
  20 sessions (1 hr)           ~1.5 Wh
  100 sessions (5 hr)          ~7.5 Wh

Brightness vs power (LED draw only — MCU/sensor ~58 mA baseline unchanged):

  BRIGHTNESS   PULSE peak   SYNC peak    Cap hit?
  100          ~350 mA      ~700 mA      No
  150          ~525 mA      ~1050 mA     Occasionally
  200          ~700 mA      ~1400 mA     Often
  255          ~893 mA      ~1780 mA     Yes — FastLED silently dims output

  Sweet spot: BRIGHTNESS 150–180 for noticeably more output without hitting the cap.
  To raise the cap: set MAX_MILLIAMPS = NUM_LEDS * 40 (2400 mA) with a 5V 3A supply.

* Future AC installation power option

---

Software Stack

IDE

* Visual Studio Code

Extensions

* PlatformIO
* Claude Code (AI assistant)

Framework

* Arduino Framework

Main Libraries

* FastLED
* PulseSensorPlayground

---

PlatformIO Setup

Board Configuration

[env:seeed_xiao_esp32s3]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
monitor_speed = 115200
monitor_filters = direct

Note: monitor_filters = direct is required for ANSI colour codes to render in the terminal.
Use pio device monitor in the VS Code integrated terminal to see coloured output.

---

Current Features

Heartbeat Detection

* Reads analog pulse data via PulseSensorPlayground (12-bit ADC, 0–4095)
* Earliest contact hint: 2 consecutive raw samples near zero triggers isPossibleContact()
* Contact quality: rolling variance + EMA baseline deviation over 800 ms window
* Contact hold timer: stays active through brief finger shifts — no false drops
* Contact confirmation: must hold steady contact before visuals respond (prevents false positives)
* Beat history: rolling window of valid beats used to compute stable BPM
* BPM expires after ~8 s with no beat, dropping back to the gather phase

LED Behaviour — State Machine

The strip runs a 7-phase state machine. All phases use red tones except the SYNC sequence. There is no purple in the current build.

IDLE (no finger detected)
* A 5-LED red pulse sweeps back and forth across the strip, centre LED brightest (brightness profile: 12, 45, 110, 45, 12)
* Signals the installation is on and waiting

POSSIBLE (earliest finger hint)
* Triggered by 2 consecutive near-zero ADC samples
* The scanner "lands": first 3 LEDs hold steady
* Prompts the user to hold still and confirm placement

GATHER (contact confirmed, collecting beats)
* Starts at 3 solid LEDs (= NUM_LEDS / 6), tip showing a 3-LED VU meter that bounces 0→3→0 at 70 BPM rate to signal "waiting for next beat"
* On each confirmed beat: a 3-LED pulse launches from the current fill edge and travels to the new proportional target
  - 1/4 beats → 25% of strip, 2/4 → 50%, 3/4 → 75%
  - Fill commits when pulse arrives; tip resumes VU meter bounce
* When all beats are collected and BPM locks, remaining LEDs fill rapidly to full strip

HOLD (stable BPM locked)
* All LEDs solid red (brightness 200)
* 500 ms pause before entering pulse mode

PULSE (beat-locked heartbeat animation)
* On each beat: strip snaps bright red, decays quadratically to a dim glow over the inter-beat interval
* Period smoothed with 80/20 EMA — tempo changes feel gradual, never jump
* Degradation: if beats become overdue the strip shrinks from the right, always leaving at least 1 LED
  - Starts degrading at 1.5× expected interval, fully shrunk at 4× interval
  - A new beat snaps the strip back to full immediately
* If beat history expires (~8 s no beat), drops back to GATHER for rebuild

DRAIN (contact lost)
* Strip wipes right-to-left to black over 800 ms
* If contact returns mid-drain, jumps straight back to PULSE

SYNC (two-sensor synchronization — testable via serial)
* ~28-second artistic sequence triggered when both participants' heartbeats synchronize
* 0–3 s     Pulses: 3 full-strip red flashes, bright to dark
* 3–4.6 s   Wipe: LEDs turn off from both ends inward until only the centre 3 remain
* 4.6–7.6 s Shift: centre 3 transition red → orange → yellow → magenta
* 7.6–10.6 s Storm: frantic magenta sparks scatter across the full strip (electrical storm feel)
* 10.6–13.6 s Zaps: 10 rapid pulses fire simultaneously from the centre toward both ends
* 13.6–14.6 s Rise: strip fades up from black to full red
* 14.6–19.6 s Glow: full strip pulses to the participant's live BPM (quadratic decay, same feel as PULSE state; falls back to 70 BPM if no reading)
* 19.6–24.6 s Fade: slow fade to black
* 24.6–27.6 s Silence: 3 seconds of darkness before returning to IDLE
* Returns to IDLE when complete, or cancels immediately if triggered again

---

Touch Feedback Design

The LED is the only feedback channel — the user cannot see the serial monitor. The state machine is intentionally designed to train finger placement without any instructions.

What the user sees → What it means

  Red scanner bouncing             No finger detected — find the sensor
  3 steady LEDs                    Finger detected, hold still to confirm
  Solid fill + 3-LED VU tip        Contact confirmed — keep holding, collecting beats
  Traveling pulse + fill extends   Beat detected — count advancing
  LEDs filling rapidly             Stable heartbeat locked, entering pulse mode
  Full strip pulsing to heartbeat  Tracking — stay still
  Strip shrinking from right       Beats stopping — adjust finger pressure or position
  Strip wiping to black            Contact lost — try again
  Red pulses → wipe → storm → zaps  Sync sequence triggered (two-sensor sync)

Training loop: if a participant shifts their finger and loses signal, the strip shrinks in real time. They see it degrade and learn to move back to the optimal position. The installation teaches itself without any screen or verbal instruction.

---

Key Tunable Constants (include/led_controller.h)

NUM_LEDS              60                             Total LEDs; each sensor owns half (30 LEDs)
BRIGHTNESS            100                            0–255 global FastLED brightness; see power table above
MAX_MILLIAMPS         NUM_LEDS × 25 = 1500 mA        FastLED power cap; raise to NUM_LEDS × 40 with a 3A supply
CONNECTING_FILL_MS    2500                           Rise speed for final fill on BPM lock (ms)
CONNECTING_HOLD_MS    500                            Hold duration in HOLD phase before PULSE (ms)
DRAIN_MS              800                            Duration of edge-to-centre wipe on contact loss (ms)

Internal constants (src/led_controller.cpp — not in header)

HALF_LEDS             NUM_LEDS / 2 = 30              LEDs per sensor half
HALF_GATHER_INIT      HALF_LEDS / 6 = 5              Starting floor in GATHER per half
GATHER_BEAT_TOTAL     4                              Must match BPM_HISTORY_SIZE; maps beat fraction → fill fraction

Key Tunable Constants (src/heartbeat.cpp)

CONTACT_CONFIRM_MS    How long to hold before state is confirmed (default 2000 ms)
CONTACT_HOLD_MS       How long to stay active after contact drops (default 2500 ms)
CONTACT_MIN_RANGE     Signal swing required to count as contact (default 200 ADC counts)
BPM_HISTORY_SIZE      Beats collected before stable BPM is declared (default 4)

---

Serial Monitor Output

Example (view in integrated terminal with pio device monitor):

CONTACT: GOOD | RAW: 1845 | IBI: 823ms | BPM: 72
♥  Beat detected! | RAW: 1845 | BPM: 72 | Stable: 71
⚠️  Beat ignored | IBI: 252ms

Serial commands (case-insensitive, type in the serial monitor):

  c   Run contact calibration — samples noise floor then three finger-on rounds, prints recommended CONTACT_MIN_RANGE
  s   Trigger the SYNC animation sequence (~28 s) — type again to cancel and reset immediately

---

Code Structure

src/main.cpp              — Main loop: reads sensor, drives all LED layers
src/heartbeat.cpp         — Sensor reading, BPM averaging, contact state machine
src/led_controller.cpp    — Full 7-phase LED animation state machine (incl. SYNC sequence)
src/connection_pulse.cpp  — Travelling pulse animation (reserved for two-sensor mode)
src/sync.cpp              — Sync state tracking between two participants

include/heartbeat.h
include/led_controller.h
include/connection_pulse.h
include/sync.h

---

Development Roadmap

Phase 1 — Complete

* ESP32 setup and LED strip control
* Heartbeat sensor reading with threshold tuning
* Signal variance contact detection
* Rolling BPM average and stable BPM gate

Phase 2 — In Progress

* Full 7-phase LED state machine (IDLE → POSSIBLE → GATHER → HOLD → PULSE → DRAIN → SYNC) ✓
* Beat-locked pulse animation with quadratic decay ✓
* Degradation system: strip shrinks when beats are overdue ✓
* Touch training feedback via LED state alone ✓
* isPossibleContact() earliest-hint detection ✓
* Auto-scaling constants (MAX_MILLIAMPS, GATHER_LEDS_PER_BEAT, GATHER_BEAT_TOTAL) ✓
* GATHER: proportional beat fill (1/4 → 25%, 2/4 → 50%, etc.) + 3-LED traveling pulse per beat ✓
* GATHER: 3-LED VU meter bounce while waiting for next beat ✓
* SYNC: ~28-second artistic sequence (pulses → wipe → colour shift → storm → zaps → glow → fade) ✓
* SYNC: triggerable via serial 's'/'S', cancelable by typing again ✓
* Second heartbeat sensor (Person B on D8) ✓
* Split strip — Sensor A drives LEDs 0–29, Sensor B drives LEDs 59–30 (reversed); each half independent ✓
* Serial output in two columns (Sensor A left, Sensor B right) ✓
* Dual pulse animation — pulses travelling from each person's end toward centre ✓
* Sync detection — auto-triggers when both BPMs are within 10 BPM of each other ✓
* Sync bloom — warm gold bloom at centre when sync is possible ✓

Phase 3

* Non-screen feedback for bad touch (audio tone, haptic motor)
* Resonance speaker testing
* Heartbeat audio pulses
* Reactive vibration surface
* Proximity sensing (VL53L0X)

Phase 4

* Installation enclosure with finger cutouts
* Long-duration stability testing (3+ days)
* Festival-ready deployment

---

Wiring Notes

WS2812B

LED Strip    ESP32
DIN          D2 (via 300–470 Ω resistor)
5V           5V (direct from USB-C — do not power through the ESP32)
GND          GND

Pulse Sensor

Sensor       ESP32
Signal       D3 (Analog)
VCC          3.3V
GND          GND

Sensor B (Person B)

Sensor       ESP32
Signal       D8 (Analog, ADC1_6)
VCC          3.3V
GND          GND

---

Upload Tips

If uploads fail:

1. Hold the BOOT button
2. Tap RESET
3. Release RESET
4. Release BOOT
5. Upload again

---

Common Issues

Serial Port Locked

Close the PlatformIO serial monitor panel, then run pio device monitor in the integrated terminal instead.

Only First LED Works

Usually caused by:

* wrong LED chipset (must be WS2812B)
* incorrect data pin
* insufficient power — power the strip directly from USB-C, not through the ESP32

ANSI Colours Not Showing

The PlatformIO serial panel does not render ANSI codes. Use the VS Code integrated terminal and run pio device monitor — colours will display correctly.

---

Project Vision

This project sits between:

* interactive art
* nervous system regulation
* biofeedback
* ambient environments
* human connection
* embodied technology

The long-term goal is a durable installation suitable for:

* festivals
* galleries
* healing spaces
* workshops
* immersive environments

---

Author

Victor Lai
