#include "purr_idf_compat.h"
#include "kitt.h"
#include "device_config.h"
#ifdef PURR_HAS_SHELL
#include "drv_shell.h"
#endif
#ifdef PURR_HAS_LUA
#include "modules/lua_runtime.h"
#endif
#ifdef PURR_HAS_MINIWIN
#include "miniwin.h"
#endif

KITT kitt;

extern void system_start();

void setup() {
    Serial.begin(115200);

    if (!kitt.init("/system/kernel/device.json")) {
        Serial.println("[KITT] FATAL: init failed");
        kitt.emergency_text("KITT INIT FAIL", "See serial log", "Hold PWR to recover");
        while (true) delay(1000);
    }

#ifdef PURR_HAS_LUA
    lua_runtime_init();
#endif
#ifdef PURR_HAS_MINIWIN
    mw_init();
#endif

#ifdef PURR_HAS_SHELL
    purr_shell_start();
#endif

    system_start();
}

void loop() {
    kitt.update();
#ifdef PURR_HAS_MINIWIN
    mw_process_message();
    delay(MW_TICK_PERIOD_MS);
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
