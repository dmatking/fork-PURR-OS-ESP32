#include <Arduino.h>
#include <lvgl.h>
#include "kitt.h"
#include "device_config.h"

KITT kitt;

extern void system_start();

void setup() {
    Serial.begin(115200);

    lv_init();

    if (!kitt.init("/system/kernel/device.json")) {
        Serial.println("[KITT] FATAL: init failed");
        kitt.emergency_text("KITT INIT FAIL", "See serial log", "Hold PWR to recover");
        while (true) delay(1000);
    }

    // Spawn system.meow (which spawns bridge + explorer)
    system_start();
}

void loop() {
    kitt.update();
    lv_timer_handler();
    delay(5);
}
