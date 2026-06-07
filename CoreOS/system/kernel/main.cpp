#include "purr_idf_compat.h"
#include "kitt.h"
#include "device_config.h"
#ifdef PURR_HAS_LUA
#include "modules/lua_runtime.h"
#endif
#ifdef PURR_HAS_LVGL
#include "modules/purr_wm.h"
#endif

#ifdef PURR_HAS_LVGL
#include <lvgl.h>
#include "modules/touch_cst816s.h"
#endif

KITT kitt;

extern void system_start();

#ifdef PURR_HAS_LVGL
// LVGL touch input callback — reads CST816S and feeds into LVGL
static void lvgl_touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
    cst_touch_event_t ev;
    touch_cst816s_get_event(&ev);
    data->point.x = ev.x;
    data->point.y = ev.y;
    data->state   = ev.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
#endif

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

#ifdef PURR_HAS_LVGL
    // Register touch input device with LVGL
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&indev_drv);
    Serial.println("[main] LVGL touch input registered");
#endif

#ifdef PURR_HAS_LUA
    lua_runtime_init();
#endif
#ifdef PURR_HAS_LVGL
    purr_wm_init();
#endif

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

// ── ESP-IDF entry point ───────────────────────────────────────────────────────
static void loop_task(void* arg) {
    (void)arg;
    while (true) loop();
}

extern "C" void app_main() {
    setup();
    xTaskCreatePinnedToCore(loop_task, "loop", 4096, NULL, tskIDLE_PRIORITY + 1, NULL, 0);
}
