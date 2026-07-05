// cupcake_module.c — Cupcake kernel module entry
//
// Forked from cardstack_module.c — same task/lock/tick shape, different
// backend guard and window register hook.

#include "cupcake.h"
#include "../../kernel/core/purr_module.h"
#include "../../kernel/core/purr_kernel.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "cupcake";

static TaskHandle_t s_task = NULL;

static void cupcake_task(void *arg)
{
    (void)arg;

    // Same rationale as cardstack_task: wait for app_manager's registry to
    // be final before building any home/drawer tiles from it.
    while (!purr_kernel_boot_ready()) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    cupcake_ui_init();

    uint32_t tick = 0;
    while (1) {
        purr_kernel_ui_lock();
        lv_tick_inc(5);
        lv_timer_handler();
        if (++tick % 40 == 0) cupcake_ui_tick();  // ~200ms
        purr_kernel_ui_unlock();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

int cupcake_init(void)
{
#ifndef CONFIG_PURR_UI_BACKEND_CUPCAKE
    ESP_LOGI(TAG, "Cupcake built-in but not selected for this device — skipping");
    return 0;
#endif

    if (purr_kernel_ui()) {
        ESP_LOGW(TAG, "UI catcall already registered — skipping Cupcake");
        return 0;
    }

    if (cupcake_hal_init() != 0) return -1;

    extern void cupcake_win_register(void);
    cupcake_win_register();

    BaseType_t ret = xTaskCreate(cupcake_task, "cupcake", 12288, NULL, 4, &s_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create cupcake task");
        return -1;
    }

    ESP_LOGI(TAG, "Cupcake ready (%ux%u)", cupcake_hal_width(), cupcake_hal_height());
    return 0;
}

void cupcake_deinit(void)
{
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    // lv_deinit() only exists when LV_MEM_CUSTOM is off (or GC is on) —
    // see lv_obj.c's matching guard. Cupcake never actually gets unloaded
    // at runtime today, so this is defensive rather than load-bearing.
#if LV_ENABLE_GC || !LV_MEM_CUSTOM
    lv_deinit();
#endif
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(cupcake) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_UI,
    .load_priority     = PURR_PRIORITY_IMPORTANT,
    .name              = "cupcake",
    .version           = "0.1.0",
    .kernel_min        = "1.0.0",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = CATCALL_FLAG_DISPLAY,
    .init              = cupcake_init,
    .deinit            = cupcake_deinit,
};
