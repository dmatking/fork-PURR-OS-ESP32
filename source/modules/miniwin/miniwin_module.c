// miniwin_module.c — PURR OS .purr module wrapper for MiniWin
//
// This is the kernel entry point for the MiniWin windowing system.
// The kernel calls init() after driver_manager has registered catcalls,
// so display and touch are guaranteed to be available by the time we run.

#include "../../kernel/core/purr_module.h"
#include "../../kernel/core/purr_kernel.h"
#include "MiniWin/miniwin.h"
#include "MiniWin/hal/hal_timer.h"
#include "MiniWin/hal/hal_non_vol.h"
#include "MiniWin/hal/hal_touch.h"
#include "MiniWin/hal/hal_init.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "miniwin";

static TaskHandle_t s_task = NULL;

static void miniwin_task(void *arg)
{
    (void)arg;

    // Initialise HAL subsystems
    mw_hal_non_vol_init();
    mw_hal_timer_init();
    mw_hal_lcd_init();
    mw_hal_touch_init();

    // Initialise MiniWin window manager
    mw_init();
    ESP_LOGI(TAG, "window manager ready (%dx%d)",
             mw_hal_lcd_get_display_width(), mw_hal_lcd_get_display_height());

    // MiniWin message pump
    while (1) {
        mw_process_message();
        taskYIELD();
    }
}

static int miniwin_init(void)
{
    const catcall_display_t *disp  = purr_kernel_display();
    const catcall_touch_t   *touch = purr_kernel_touch();

    if (!disp) {
        ESP_LOGE(TAG, "no display catcall — miniwin cannot start");
        return -1;
    }
    if (!touch) {
        ESP_LOGW(TAG, "no touch catcall — touch input disabled");
    }

    // Run MiniWin in its own task (needs a real stack — message pump + rendering)
    BaseType_t ret = xTaskCreate(miniwin_task, "miniwin",
                                 8192, NULL, 5, &s_task);
    return (ret == pdPASS) ? 0 : -1;
}

static void miniwin_deinit(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
}

// ── .purr module header ───────────────────────────────────────────────────────

purr_module_header_t purr_module = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_UI,
    .name              = "miniwin",
    .version           = "0.1.0",
    .kernel_min        = "0.9.0",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = CATCALL_FLAG_DISPLAY,   // touch is optional
    .init              = miniwin_init,
    .deinit            = miniwin_deinit,
};
