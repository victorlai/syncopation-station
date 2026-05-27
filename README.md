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

Current development includes:

* dual heartbeat sensing
* WS2812B LED animations
* threshold and signal filtering
* startup / idle states
* future proximity sensing and audio feedback

⸻

Hardware

Controller

* Seeed Studio XIAO ESP32S3

Sensors

* 2x Pulse Sensor Amped heartbeat sensors
* Optional VL53L0X / VL53L1X distance sensor

Lighting

* WS2812B LED Strip
* Diffusion tubing / silicone neon tubing

Audio (planned)

* Resonance speaker / exciter
* PAM8403 amplifier module

Power

* USB-C power
* Portable battery bank
* Future AC installation power option

⸻

Software Stack

IDE

* Visual Studio Code

Extensions

* PlatformIO
* GitHub Copilot

Framework

* Arduino Framework

Main Libraries

FastLED
Wire
PulseSensorPlayground (optional)

⸻

PlatformIO Setup

Board Configuration

Use:

[env:seeed_xiao_esp32s3]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
monitor_speed = 115200

⸻

Current Features

Heartbeat Detection

* Reads analog pulse data
* Detects beats using thresholds
* Calculates BPM
* Filters false readings

LED Feedback

* Pulse-reactive brightness
* Startup animations
* Rainbow flowing idle effects
* Future dual-person synchronization effects

Serial Monitor States

Example output:

❤️ BEAT | BPM: 78
✅ READY / MONITORING
⚠️ POSSIBLE CONTACT LOST

⸻

Development Roadmap

Phase 1

* ESP32 setup
* LED strip control
* Heartbeat sensor reading
* Threshold tuning
* Two simultaneous heartbeat sensors
* Stable BPM averaging

Phase 2

* LED pulse mapped to heartbeat
* Shared synchronization patterns
* Ambient idle states
* Startup / connection sequence

Phase 3

* Resonance speaker testing
* Heartbeat audio pulses
* Reactive vibration surface
* Proximity sensing

Phase 4

* Installation enclosure
* Power optimization
* Long-duration stability testing
* Festival-ready deployment

⸻

Wiring Notes

WS2812B

LED Strip	ESP32
DIN	D2
5V	5V
GND	GND

Pulse Sensor

Sensor	ESP32
Signal	D3 (Analog)
VCC	3.3V
GND	GND

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

Close:

* Serial Monitor
* Arduino IDE
* other VSCode windows

Then reconnect the board.

Only First LED Works

Usually caused by:

* wrong LED chipset
* incorrect data pin
* insufficient power
* damaged first LED

Copilot Chat Errors

If you see:

Cannot read properties of undefined (reading 'bind')

Try:

* reloading VSCode
* reinstalling Copilot Chat
* disabling conflicting AI extensions

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
