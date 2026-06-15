// miniwin_module.c — PURR OS .purr module wrapper for MiniWin
//
// This is the kernel entry point for the MiniWin windowing system.
// The kernel calls init() after driver_manager has registered catcalls,
// so display and touch are guaranteed to be available by the time we run.
//
// Only activates when CONFIG_PURR_UI_BACKEND_MINIWIN=y (set in device sdkconfig).
// If another UI module has already claimed the catcall_ui slot, init() returns 0
// without starting MiniWin.

#include "../../kernel/core/purr_module.h"
#include "../../kernel/core/purr_kernel.h"
#include "MiniWin/miniwin.h"
#include "MiniWin/hal/hal_timer.h"
#include "MiniWin/hal/hal_non_vol.h"
#include "MiniWin/hal/hal_touch.h"
#include "MiniWin/hal/hal_init.h"
#include "MiniWin/hal/hal_lcd.h"
#include "MiniWin/gl/gl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

extern void miniwin_win_register(void);

#include "miniwin_cursor.h"
#include "miniwin_keyboard.h"

// MiniWin framework callbacks — the library calls these at init and repaint time.
void mw_user_init(void) {}

void mw_user_root_paint_function(const mw_gl_draw_info_t *draw_info)
{
    // Fill the desktop background. MiniWin does not clear the root window
    // automatically — without this the display shows stale content.
    mw_gl_set_solid_fill_colour(MW_HAL_LCD_GREY5);
    mw_gl_clear_pattern();
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_rectangle(draw_info, 0, 0,
                    mw_hal_lcd_get_display_width(),
                    mw_hal_lcd_get_display_height());
}

void mw_user_root_message_function(const mw_message_t *message) { (void)message; }

// Forward declaration — app_manager may not be loaded; guard with get_module.
extern void app_manager_open_launcher(void);

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
    int disp_w = mw_hal_lcd_get_display_width();
    int disp_h = mw_hal_lcd_get_display_height();
    ESP_LOGI(TAG, "window manager ready (%dx%d)", disp_w, disp_h);

    // Init trackball cursor overlay
    miniwin_cursor_init(disp_w, disp_h);

    // Open Cat Apps launcher — safe to call now that mw_add_window() works
    if (purr_kernel_get_module("app_manager")) {
        app_manager_open_launcher();
    }

    // MiniWin message pump
    while (1) {
        mw_process_message();
        miniwin_keyboard_poll();  // drain all inputs: cursor gets pointer/click, keys → focused win
        miniwin_cursor_poll();    // redraw cursor on top of frame if position changed
        taskYIELD();
    }
}

static int miniwin_init(void)
{
#ifndef CONFIG_PURR_UI_BACKEND_MINIWIN
    ESP_LOGI(TAG, "MiniWin built-in but not selected for this device — skipping");
    return 0;
#endif

    if (purr_kernel_ui()) {
        ESP_LOGW(TAG, "UI catcall already registered — skipping MiniWin");
        return 0;
    }

    const catcall_display_t *disp  = purr_kernel_display();
    const catcall_touch_t   *touch = purr_kernel_touch();

    if (!disp) {
        ESP_LOGE(TAG, "no display catcall — miniwin cannot start");
        return -1;
    }
    if (!touch) {
        ESP_LOGW(TAG, "no touch catcall — touch input disabled");
    }

    // Register catcall_ui_t so apps can use purr_win_*() regardless of task state
    miniwin_win_register();

    // Run MiniWin message pump in its own task
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

PURR_MODULE_REGISTER(miniwin) = {
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
