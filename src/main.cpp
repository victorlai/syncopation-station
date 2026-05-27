#include <Arduino.h>
#include "led_controller.h"
#include "heartbeat.h"

void setup() {

    // Initialize LED strip
    setupLedController();

    // Initialize heartbeat sensor
    setupHeartbeat();
}


void loop() {
    uint8_t beat = heartbeatBrightness();
    bool contact = getContactGood();

    drawFrame(beat, contact);
    showLeds();
    delay(20);
}

