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
#include <stdio.h>

extern void miniwin_win_register(void);

#include "miniwin_cursor.h"
#include "miniwin_keyboard.h"
#include "../app_manager/app_manager.h"

static const char *TAG = "miniwin";

// CONFIG_PURR_UI_WINCE_SHELL devices bake their own WinCE shell directly into
// the kernel (no .purr module wrapper) and provide mw_user_init() /
// mw_user_root_paint_function() / mw_user_root_message_function() /
// the MiniWin task themselves — defining them here too would be a duplicate
// symbol. This module still only compiles in when CONFIG_PURR_UI_BACKEND_MINIWIN
// is set, so non-WinCE devices are unaffected.
#ifndef CONFIG_PURR_UI_WINCE_SHELL

// MiniWin framework callbacks — the library calls these at init and repaint time.
void mw_user_init(void) {}

// ── Desktop icons ────────────────────────────────────────────────────────────
// Basic app shortcuts drawn directly on the root window. Tapping/clicking one
// launches that app via app_manager. Reuses app_manager's existing registry —
// no separate icon list to keep in sync.
#define DESKTOP_ICON_W      48
#define DESKTOP_ICON_H      48
#define DESKTOP_ICON_GAP    8
#define DESKTOP_ICON_COLS   5
#define DESKTOP_ICON_LEFT   8

// ── Status bar ───────────────────────────────────────────────────────────────
// Single strip across the top of the desktop: free RAM, WiFi, LoRa, battery.
// MiniWin-only — the mobile_ui kernel has its own separate status bar.
#define STATUS_BAR_H        14
#define DESKTOP_ICON_TOP    (STATUS_BAR_H + 6)

static void draw_status_bar(const mw_gl_draw_info_t *draw_info)
{
    int w = mw_hal_lcd_get_display_width();
    char buf[24];

    mw_gl_set_solid_fill_colour(MW_HAL_LCD_BLACK);
    mw_gl_clear_pattern();
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_rectangle(draw_info, 0, 0, w, STATUS_BAR_H);

    snprintf(buf, sizeof(buf), "RAM %uK", (unsigned)(purr_kernel_free_ram() / 1024));
    mw_gl_set_solid_fill_colour(MW_HAL_LCD_WHITE);
    mw_gl_string(draw_info, 2, 3, buf);

    bool wifi = purr_kernel_wifi_connected();
    mw_gl_set_solid_fill_colour(wifi ? MW_HAL_LCD_GREEN : MW_HAL_LCD_RED);
    mw_gl_string(draw_info, w - 132, 3, "WiFi");

    bool lora = purr_kernel_lora_available();
    mw_gl_set_solid_fill_colour(lora ? MW_HAL_LCD_GREEN : MW_HAL_LCD_RED);
    mw_gl_string(draw_info, w - 92, 3, "LoRa");

    int batt = purr_kernel_battery_percent();
    if (batt >= 0) {
        snprintf(buf, sizeof(buf), "Bat %d%%", batt);
    } else {
        snprintf(buf, sizeof(buf), "Bat --");
    }
    mw_gl_set_solid_fill_colour(MW_HAL_LCD_WHITE);
    mw_gl_string(draw_info, w - 52, 3, buf);
}

void mw_user_root_paint_function(const mw_gl_draw_info_t *draw_info)
{
    ESP_LOGI(TAG, "DBG root_paint_function called, clip=(%d,%d,%d,%d)",
             draw_info->clip_rect.x, draw_info->clip_rect.y,
             draw_info->clip_rect.width, draw_info->clip_rect.height);
    // Fill the desktop background. MiniWin does not clear the root window
    // automatically — without this the display shows stale content.
    mw_gl_set_solid_fill_colour(MW_HAL_LCD_GREY5);
    mw_gl_clear_pattern();
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_rectangle(draw_info, 0, 0,
                    mw_hal_lcd_get_display_width(),
                    mw_hal_lcd_get_display_height());

    draw_status_bar(draw_info);

    if (!purr_kernel_get_module("app_manager")) return;

    int count = app_manager_count();
    for (int i = 0; i < count; i++) {
        const app_entry_t *app = app_manager_get(i);
        if (!app) continue;

        int col = i % DESKTOP_ICON_COLS;
        int row = i / DESKTOP_ICON_COLS;
        int x = DESKTOP_ICON_LEFT + col * (DESKTOP_ICON_W + DESKTOP_ICON_GAP);
        int y = DESKTOP_ICON_TOP  + row * (DESKTOP_ICON_H + DESKTOP_ICON_GAP + 12);

        mw_gl_set_solid_fill_colour(MW_HAL_LCD_GREY3);
        mw_gl_set_border(MW_GL_BORDER_ON);
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_rectangle(draw_info, x, y, DESKTOP_ICON_W, DESKTOP_ICON_H);

        mw_gl_string(draw_info, x, y + DESKTOP_ICON_H + 1, app->name);
    }
}

// Forward declaration — app_manager may not be loaded; guard with get_module.
extern void app_manager_open_launcher(void);

void mw_user_root_message_function(const mw_message_t *message)
{
    if (!message || message->message_id != MW_TOUCH_DOWN_MESSAGE) return;
    if (!purr_kernel_get_module("app_manager")) return;

    int16_t tx = (int16_t)(message->message_data >> 16);
    int16_t ty = (int16_t)(message->message_data & 0xFFFF);

    int count = app_manager_count();
    for (int i = 0; i < count; i++) {
        int col = i % DESKTOP_ICON_COLS;
        int row = i / DESKTOP_ICON_COLS;
        int x = DESKTOP_ICON_LEFT + col * (DESKTOP_ICON_W + DESKTOP_ICON_GAP);
        int y = DESKTOP_ICON_TOP  + row * (DESKTOP_ICON_H + DESKTOP_ICON_GAP + 12);

        if (tx >= x && tx < x + DESKTOP_ICON_W && ty >= y && ty < y + DESKTOP_ICON_H) {
            ESP_LOGI(TAG, "desktop icon %d tapped — launching", i);
            app_manager_launch_idx(i);
            return;
        }
    }
}

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

    // Desktop boots empty with app icons (drawn in mw_user_root_paint_function).
    // Launcher now opens on demand: Enter key with nothing focused, or tapping
    // the start-menu icon/desktop icon directly.
    //
    // mw_init() posts a WINDOW_CREATED message for the root window, but that
    // message's handling doesn't actually trigger a paint by itself — without
    // an explicit initial paint here the screen just stays whatever the
    // display driver's own GRAM-clear left it as (black) until SOMETHING
    // else happens to repaint the root window. Force the first paint now.
    mw_paint_window_client(MW_ROOT_WINDOW_HANDLE);

    // MiniWin message pump
    TickType_t last_status_redraw = xTaskGetTickCount();
    mw_util_rect_t status_rect = { 0, 0, (int16_t)disp_w, STATUS_BAR_H };

    while (1) {
        mw_process_message();
        miniwin_keyboard_poll();  // drain all inputs: cursor gets pointer/click, keys → focused win
        miniwin_cursor_poll();    // redraw cursor on top of frame if position changed

        // Refresh the status bar (RAM/WiFi/LoRa/battery) once a second —
        // these change slowly, no need to redraw on every tick.
        TickType_t now = xTaskGetTickCount();
        if ((now - last_status_redraw) >= pdMS_TO_TICKS(1000)) {
            last_status_redraw = now;
            mw_paint_window_client_rect(MW_ROOT_WINDOW_HANDLE, &status_rect);
        }

        taskYIELD();
    }
}

#endif  // !CONFIG_PURR_UI_WINCE_SHELL

static int miniwin_init(void)
{
#ifndef CONFIG_PURR_UI_BACKEND_MINIWIN
    ESP_LOGI(TAG, "MiniWin built-in but not selected for this device — skipping");
    return 0;
#endif

#ifdef CONFIG_PURR_UI_WINCE_SHELL
    // WinCE shell is started directly from kernel_atdp_boot.cpp (baked in,
    // no module wrapper) — this module has nothing left to do for it.
    ESP_LOGI(TAG, "WinCE shell baked into kernel — miniwin module skipping");
    return 0;
#else

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
#endif  // CONFIG_PURR_UI_WINCE_SHELL
}

static void miniwin_deinit(void)
{
#ifndef CONFIG_PURR_UI_WINCE_SHELL
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
#endif
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
