// cupcake_module.c — Cupcake kernel module entry
//
// Forked from cardstack_module.c — same task/lock/tick shape, different
// backend guard and window register hook.

#include "cupcake.h"
#include "../../kernel/core/purr_module.h"
#include "../../kernel/core/purr_kernel.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "cupcake";

static TaskHandle_t s_task = NULL;

// Dedicated static internal-RAM stack — cupcake_task is the LVGL render/
// dispatch loop every app's button/widget callback actually runs on
// (lv_timer_handler -> lv_event_send -> the app's purr_win_cb_t), including
// any NVS/flash-touching callback (e.g. settings.c's nvs_save_str/
// nvs_save_u8 on its theme/brightness/wallpaper buttons) — confirmed live
// via a real crash backtrace (cupcake_task -> lv_timer_handler ->
// lv_indev_read_timer_cb -> lv_event_send -> btn_event_cb -> on_bt_scan ->
// bt_mgr_scan -> xQueueSemaphoreTake). A PSRAM-backed stack here would
// crash the instant any such callback disables the flash cache — see
// app_manager.c's "static stack pool" comment for the full mechanism. This
// puts cupcake_task's own stack in the same protected category as
// settings/fileman's, not a PSRAM-eligible app task — it's core UI-kernel
// dispatch structure, not "just another app."
//
// Sized at 8192 (down from the previous dynamic allocation's 12288) —
// static internal-RAM reservations are fixed at link time, and 12288 here
// on top of settings/fileman's existing 8192-each static stacks overflowed
// the internal DRAM segment by 3408 bytes at link time. 8192 matches the
// size already proven safe for settings/fileman's own (arguably more
// complex, NVS+VFS-touching) code paths.
#define CUPCAKE_STACK_SIZE 8192
static StackType_t  s_cupcake_stack[CUPCAKE_STACK_SIZE];
static StaticTask_t s_cupcake_tcb;

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
        int64_t t0 = esp_timer_get_time();
        lv_timer_handler();
        int64_t handler_us = esp_timer_get_time() - t0;
        // Cheap, always-on: a single call taking this long means whatever
        // ran inside it (a widget event callback, an app's init() work
        // dispatched through here) visibly stalled rendering for that
        // long — real data to point at if something feels sluggish again,
        // instead of guessing. 50ms was picked as "a dropped frame you'd
        // actually notice", not tuned against a specific measurement.
        if (handler_us > 50000) {
            ESP_LOGW(TAG, "lv_timer_handler() took %lldms (tick=%lu)",
                     (long long)(handler_us / 1000), (unsigned long)tick);
        }
        if (++tick % 40 == 0) cupcake_ui_tick();  // ~200ms
        purr_kernel_ui_unlock();
        purr_kernel_ui_heartbeat();
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

    s_task = xTaskCreateStatic(cupcake_task, "cupcake", CUPCAKE_STACK_SIZE, NULL, 4,
                                s_cupcake_stack, &s_cupcake_tcb);
    if (!s_task) {
        ESP_LOGE(TAG, "xTaskCreateStatic failed for cupcake task");
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
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = CATCALL_FLAG_DISPLAY,
    .init              = cupcake_init,
    .deinit            = cupcake_deinit,
};
