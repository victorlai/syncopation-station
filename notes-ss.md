# Personal build notes

## Current version
https://github.com/WorldFamousElectronics/PulseSensorPlayground/tree/master/examples/Getting_BPM_to_Monitor

# To Do


# Tuning

CONNECTING_FILL_MS = 1500 — total fill duration
CONNECTING_HOLD_MS = 500 — hold before pulsing
CONNECTING_START_BEATS = 2 — beats needed to trigger (must be ≤ BPM_HISTORY_SIZE)


# CODE - TO DO

Add an indicator to train user to press the right way.

Power: Set max LED brightness power


# ELECTRONICS - TO DO

Test with external power supply.
Power WS2812B directly to USB-C. Strip a cable USB-C or micro USB cable


# BUILD - TO DO

Create box with finger cutout
Create clip

# Startup 
git pull

# Commit process
git add .
git commit -m "Describe_what_changed"

3-10 commits/day
* commit BEFORE risky experiments, AFTER something finally works, BEFORE switching computers

*end of day:*
git push

# 2nd Computer
git clone https://github.com/YOURNAME/syncopation-station.git
