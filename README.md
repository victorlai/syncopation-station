Syncopation Station

Heartbeat Reactive Art Installation using ESP32-S3, LEDs, Sensors & Sound

Overview

Syncopation Station is an interactive art installation exploring connection, rhythm, and co-regulation through light and sound.

Participants place their fingers on heartbeat sensors. Their pulse data is read in real time using an ESP32-S3 microcontroller and translated into reactive LED animations, ambient lighting, and eventually sound/vibration feedback.

The installation is designed to:

* encourage slowing down and presence
* visualize synchronization between two people
* create an immersive glowing pulse environment
* blend technology with embodied human experience

⸻

Hardware

Controller

* Seeed Studio XIAO ESP32S3

Sensors

* 2x Pulse Sensor Amped heartbeat sensors (1 active, 1 pending wiring)
* Optional VL53L0X / VL53L1X distance sensor

Lighting

* WS2812B LED Strip (60 LEDs)
* Diffusion tubing / silicone neon tubing

Audio (planned)

* Resonance speaker / exciter
* PAM8403 amplifier module

Power

* USB-C power
* 20,000mAh portable battery bank (~3 day runtime at current power settings)
* Future AC installation power option

⸻

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

⸻

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

⸻

Current Features

Heartbeat Detection

* Reads analog pulse data via PulseSensorPlayground
* Detects beats using a tunable threshold (12-bit ADC, default 1930)
* Rolling 5-beat BPM average for stable readings
* Contact quality detection using signal variance over an 800ms rolling window — prevents false positives from a floating sensor
* Contact hold timer (2500ms) absorbs brief finger shifts without dropping state
* Contact confirmation delay (2000ms) — strip stays in standby until a real hold is detected

LED Behaviour

Standby (no contact)
* Slow purple breathing across the full strip (~10s per cycle)

Contact detected — warm-up (0–2s)
* Strip stays purple — prevents false positives from lighting up

Contact confirmed (after 2s)
* Strip transitions: purple fades to black, then red breathes in
* First half of strip (LEDs 0–29) glows red, pulsing at the person's BPM
* Second half (LEDs 30–59) stays dark — reserved for Person B

Connection Pulse
* On each confirmed beat, a soft red pulse spawns at the left edge and travels toward the centre
* Pulse uses a Gaussian falloff curve — no hard edges, fades organically
* Multiple pulses visible simultaneously at faster heart rates
* Pulses stop at the centre (Person B's pulses will travel inward from the right)

Release
* Red fades to black, then purple breathes back in
* Gradual transition — never jumps directly between colours

Power Management
* FastLED hard cap: 500mA total draw
* Global brightness: 100/255
* Estimated runtime: ~3 days on a 20,000mAh bank

Serial Monitor Output

Example (view in integrated terminal with pio device monitor):

CONTACT: GOOD | RAW: 1845 | IBI: 823ms | BPM: 72
♥  Beat detected! | RAW: 1845 | BPM: 72 | Stable: 71
⚠️  Beat ignored | IBI: 252ms

⸻

Code Structure

src/main.cpp          — Main loop: reads sensor, drives LED layers
src/heartbeat.cpp     — Sensor reading, BPM averaging, contact state
src/led_controller.cpp — Background layer: standby and contact colour states
src/connection_pulse.cpp — Travelling pulse animation layer

include/heartbeat.h
include/led_controller.h
include/connection_pulse.h

Key tunable constants:

CONTACT_CONFIRM_MS   heartbeat.cpp   How long to hold finger before strip responds (default 2000ms)
CONTACT_HOLD_MS      heartbeat.cpp   How long to stay active after contact drops (default 2500ms)
CONTACT_MIN_RANGE    heartbeat.cpp   Signal swing required to count as contact (default 200 ADC counts)
BPM_HISTORY_SIZE     heartbeat.cpp   Beats averaged for stable BPM (default 5)
PULSE_SPEED          connection_pulse.cpp   Pulse travel speed in LEDs/frame (default 0.5)
PULSE_SIGMA_SQ       connection_pulse.cpp   Pulse blob softness (default 6.0)
BRIGHTNESS           led_controller.h       Global LED brightness 0–255 (default 100)
MAX_MILLIAMPS        led_controller.h       Power cap in mA (default 500)

⸻

Development Roadmap

Phase 1 — Complete

* ESP32 setup and LED strip control
* Heartbeat sensor reading with threshold tuning
* Signal variance contact detection
* Rolling BPM average
* Stable BPM confirmation gate

Phase 2 — In Progress

* LED breathing mapped to heartbeat BPM ✓
* Standby / contact / release colour transitions ✓
* Connection Pulse animation (Person A) ✓
* Split strip — Person A left half, Person B right half ✓
* Second heartbeat sensor (Person B) — pending wiring
* Dual pulse animation — pulses travelling toward each other
* Sync detection — compare BPMs between participants
* Sync bloom — warm centre glow when BPMs align
* Test calming vs energetic sync visual states

Phase 3

* Resonance speaker testing
* Heartbeat audio pulses
* Reactive vibration surface
* Proximity sensing (VL53L0X)

Phase 4

* Installation enclosure
* Long-duration stability testing (3+ days)
* Festival-ready deployment

⸻

Wiring Notes

WS2812B

LED Strip    ESP32
DIN          D2
5V           5V (direct from USB-C recommended for full strip)
GND          GND

Pulse Sensor

Sensor       ESP32
Signal       D3 (Analog)
VCC          3.3V
GND          GND

Second sensor (Person B) — pin TBD when wiring added.

⸻

Upload Tips

If uploads fail:

1. Hold the BOOT button
2. Tap RESET
3. Release RESET
4. Release BOOT
5. Upload again

⸻

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

⸻

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

⸻

Author

Victor Lai
