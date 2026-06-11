#include "kitt.h"
#include "device_config.h"
#include "purr_panic.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const char *TAG = "app_main";
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
    if (!kitt.init("/system/kernel/device.json")) {
        ESP_LOGE(TAG, "KITT init failed");
        purr_panic(PURR_STOP_CATFAIL, PURR_PANIC_RED, "KITT init failed — see logs");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
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
    vTaskDelay(pdMS_TO_TICKS(MW_TICK_PERIOD_MS));
#else
    vTaskDelay(pdMS_TO_TICKS(10));
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
