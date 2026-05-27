#include <Arduino.h>
#include "led_controller.h"
#include "heartbeat.h"

static uint8_t hueOffset = 0;

void setup() {
    setupLedController();
    setupHeartbeat();
}

void loop() {
    drawRainbow(hueOffset);

    uint8_t beat = heartbeatBrightness();
    applyHeartbeatPulse(beat);

    showLeds();

    hueOffset += 4;
    delay(20);
}
