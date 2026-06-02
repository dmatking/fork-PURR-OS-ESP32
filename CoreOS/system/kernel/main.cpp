#include <Arduino.h>
#include "kitt.h"
#include "device_config.h"

#ifdef PURR_HAS_LVGL
#include <lvgl.h>
#endif

KITT kitt;

extern void system_start();

void setup() {
    Serial.begin(115200);

#ifdef PURR_HAS_LVGL
    lv_init();
#endif

    if (!kitt.init("/system/kernel/device.json")) {
        Serial.println("[KITT] FATAL: init failed");
        kitt.emergency_text("KITT INIT FAIL", "See serial log", "Hold PWR to recover");
        while (true) delay(1000);
    }

    system_start();
}

void loop() {
    kitt.update();
#ifdef PURR_HAS_LVGL
    lv_timer_handler();
    delay(5);
#else
    delay(10);
#endif
}
