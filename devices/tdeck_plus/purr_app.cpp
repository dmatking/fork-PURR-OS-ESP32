// WCE shell — tdeck_plus
// Single full-screen shell window. App windows launched via purr_catalog[].

#include "miniwin.h"
#include "miniwin_utilities.h"
#include "miniwin_settings.h"
#include "calibrate.h"
#include "hal/hal_touch.h"
#include "esp_log.h"
#include "gl/gl.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include <stdio.h>

#include "purr_app_catalog.h"
#include "purr_taskbar.h"
#include "hal_input.h"
#include "app_cursor.h"

#define SCR_W       mw_hal_lcd_get_display_width()
#define SCR_H       mw_hal_lcd_get_display_height()
#define TASKBAR_H   22
#define TASKBAR_Y   (SCR_H - TASKBAR_H)
#define START_X     2
#define START_Y     (TASKBAR_Y + 2)
#define START_W     52
#define START_H     (TASKBAR_H - 4)
#define SMENU_W     130
#define SMENU_IH    18
#define SMENU_SEP_H 8
#define SMENU_X     0

// Top-level menu: "Programs >" + separator + "Restart" + "Launcher"
#define SMENU_TL_H  (3 * SMENU_IH + SMENU_SEP_H + 4)

#define WCE_DESKTOP 0x008080
#define WCE_BAR     0xC0C0C0
#define WCE_HI      0xFFFFFF
#define WCE_SHD     0x808080
#define WCE_DARK    0x404040
#define WCE_TXT     0x000000
#define WCE_MBKG    0xD4D0C8

static mw_handle_t shell_handle;
static bool smenu_open   = false;
static int  smenu_folder = -1;  // -1 = top level, 0 = programs

static void draw_raised(const mw_gl_draw_info_t *d,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        mw_hal_lcd_colour_t fill)
{
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(fill); mw_gl_rectangle(d, x, y, w, h);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_hline(d, x, x+w-1, y); mw_gl_vline(d, x, y, y+h-1);
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, x+1, x+w-2, y+h-2); mw_gl_vline(d, x+w-2, y+1, y+h-2);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_hline(d, x, x+w-1, y+h-1); mw_gl_vline(d, x+w-1, y, y+h-1);
}

static void draw_sunken(const mw_gl_draw_info_t *d,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        mw_hal_lcd_colour_t fill)
{
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(fill); mw_gl_rectangle(d, x, y, w, h);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_hline(d, x, x+w-1, y); mw_gl_vline(d, x, y, y+h-1);
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, x+1, x+w-2, y+1); mw_gl_vline(d, x+1, y+1, y+h-2);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_hline(d, x, x+w-1, y+h-1); mw_gl_vline(d, x+w-1, y, y+h-1);
}

static void draw_smenu_box(const mw_gl_draw_info_t *d,
                           int16_t smy, int16_t smh)
{
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_MBKG);
    mw_gl_rectangle(d, SMENU_X, smy, SMENU_W, smh);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_set_fill(MW_GL_NO_FILL); mw_gl_set_border(MW_GL_BORDER_ON);
    mw_gl_rectangle(d, SMENU_X, smy, SMENU_W, smh);
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
}

static void shell_paint(mw_handle_t handle, const mw_gl_draw_info_t *d)
{
    (void)handle;

    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_DESKTOP);
    mw_gl_rectangle(d, 0, 0, SCR_W, TASKBAR_Y);

    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, TASKBAR_Y, SCR_W, TASKBAR_H);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_hline(d, 0, SCR_W - 1, TASKBAR_Y);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_hline(d, 0, SCR_W - 1, SCR_H - 1);

    if (smenu_open) draw_sunken(d, START_X, START_Y, START_W, START_H, WCE_BAR);
    else            draw_raised(d, START_X, START_Y, START_W, START_H, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, START_X + 6, START_Y + 5, "Meow!");

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_vline(d, START_X + START_W + 2, TASKBAR_Y + 2, SCR_H - 3);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_vline(d, START_X + START_W + 3, TASKBAR_Y + 2, SCR_H - 3);

    // Taskbar app buttons
    {
        int16_t area_x = (int16_t)(START_X + START_W + 6);
        int16_t area_w = (int16_t)((SCR_W - 52) - area_x);
        int n = taskbar_entry_count;
        if (n > 0 && area_w >= 22) {
            int16_t pitch = (int16_t)(area_w / n);
            int16_t bw = (int16_t)(pitch - 2);
            mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
            mw_gl_set_font(MW_GL_FONT_9);
            for (int i = 0; i < n; i++) {
                int16_t bx = (int16_t)(area_x + i * pitch);
                mw_handle_t eh = taskbar_entries[i].handle;
                bool focused = (eh == taskbar_focused_handle) &&
                               !(mw_get_window_flags(eh) & MW_WINDOW_FLAG_IS_MINIMISED);
                if (focused) draw_sunken(d, bx, START_Y, bw, START_H, WCE_BAR);
                else         draw_raised(d, bx, START_Y, bw, START_H, WCE_BAR);
                mw_gl_set_fg_colour(WCE_TXT);
                mw_gl_string(d, (int16_t)(bx + 3), START_Y + 5,
                             taskbar_entries[i].name);
            }
        }
    }

    char ram[10];
    snprintf(ram, sizeof(ram), "%ukB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    int16_t bx = SCR_W - 50;
    draw_sunken(d, bx, TASKBAR_Y + 2, 48, TASKBAR_H - 4, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, bx + 4, START_Y + 5, ram);

    if (!smenu_open) return;

    if (smenu_folder < 0) {
        // Top-level: "Programs >" + separator + "Restart"
        int16_t smy = (int16_t)(TASKBAR_Y - SMENU_TL_H);
        draw_smenu_box(d, smy, SMENU_TL_H);
        mw_gl_string(d, SMENU_X + 8, smy + 2 + 4, "Programs >");
        mw_gl_set_fg_colour(WCE_SHD);
        mw_gl_hline(d, SMENU_X + 4, SMENU_X + SMENU_W - 4,
                    (int16_t)(smy + 2 + SMENU_IH + SMENU_SEP_H / 2));
        mw_gl_set_fg_colour(WCE_TXT);
        mw_gl_string(d, SMENU_X + 8,
                     (int16_t)(smy + 2 + SMENU_IH + SMENU_SEP_H + 4), "Restart");
        mw_gl_string(d, SMENU_X + 8,
                     (int16_t)(smy + 2 + SMENU_IH * 2 + SMENU_SEP_H + 4), "Launcher");
    } else {
        // Programs submenu: "< Back" + catalog entries
        int16_t smh = (int16_t)((purr_catalog_count + 1) * SMENU_IH + 4);
        int16_t smy = (int16_t)(TASKBAR_Y - smh);
        draw_smenu_box(d, smy, smh);
        mw_gl_string(d, SMENU_X + 8, smy + 2 + 4, "< Back");
        for (int i = 0; i < purr_catalog_count; i++) {
            int16_t iy = smy + 2 + (int16_t)((i + 1) * SMENU_IH);
            mw_gl_string(d, SMENU_X + 8, iy + 4, purr_catalog[i].name);
        }
    }
}

static void shell_message(const mw_message_t *msg)
{
    if (msg->message_id == MW_WINDOW_CREATED_MESSAGE ||
        msg->message_id == MW_TIMER_MESSAGE) {
        hal_input_tick();

        // If the touch IC reports a real finger touch, hide the trackball cursor
        // so touch takes over. Cursor reappears on next trackball movement.
        if (mw_hal_touch_get_state() == MW_HAL_TOUCH_STATE_DOWN) {
            hal_input_notify_touch();
        }

        // Synthesize a touch-down event if the trackball click was pressed
        if (hal_input_click_pending()) {
            int16_t cx, cy;
            hal_input_get_cursor(&cx, &cy);
            uint32_t td = ((uint32_t)(uint16_t)cx << 16) | (uint16_t)cy;
            mw_post_message(MW_TOUCH_DOWN_MESSAGE,
                            MW_INVALID_HANDLE, shell_handle,
                            td, NULL,
                            MW_WINDOW_MESSAGE);
        }

        mw_paint_all();
        mw_set_timer(MW_TICKS_PER_SECOND, shell_handle, MW_WINDOW_MESSAGE);
        return;
    }
    if (msg->message_id != MW_TOUCH_DOWN_MESSAGE) return;

    int16_t tx = (int16_t)(msg->message_data >> 16);
    int16_t ty = (int16_t)(msg->message_data & 0xFFFF);

    if (smenu_open) {
        if (smenu_folder < 0) {
            int16_t smy = (int16_t)(TASKBAR_Y - SMENU_TL_H);
            if (tx >= SMENU_X && tx < SMENU_X + SMENU_W &&
                ty >= smy && ty < smy + SMENU_TL_H) {
                int rel_y = ty - smy - 2;
                if (rel_y >= 0 && rel_y < SMENU_IH) {
                    smenu_folder = 0;  // open Programs submenu
                } else if (rel_y >= SMENU_IH + SMENU_SEP_H &&
                           rel_y < SMENU_IH * 2 + SMENU_SEP_H) {
                    smenu_open   = false;
                    smenu_folder = -1;
                    esp_restart();
                } else if (rel_y >= SMENU_IH * 2 + SMENU_SEP_H) {
                    smenu_open   = false;
                    smenu_folder = -1;
                    // Boot into launcher (factory partition)
                    const esp_partition_t *launcher = esp_partition_find_first(
                        ESP_PARTITION_TYPE_APP,
                        ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
                    if (launcher && esp_ota_set_boot_partition(launcher) == ESP_OK)
                        esp_restart();
                    else
                        esp_restart();  // fallback: plain restart
                }
            } else {
                smenu_open   = false;
                smenu_folder = -1;
            }
        } else {
            int16_t smh = (int16_t)((purr_catalog_count + 1) * SMENU_IH + 4);
            int16_t smy = (int16_t)(TASKBAR_Y - smh);
            if (tx >= SMENU_X && tx < SMENU_X + SMENU_W &&
                ty >= smy && ty < smy + smh) {
                int item = (ty - smy - 2) / SMENU_IH;
                if (item == 0) {
                    smenu_folder = -1;  // back to top
                } else {
                    int idx = item - 1;
                    smenu_open   = false;
                    smenu_folder = -1;
                    if (idx >= 0 && idx < purr_catalog_count) {
                        purr_catalog[idx].launch();
                        mw_paint_all();
                    }
                }
            } else {
                smenu_open   = false;
                smenu_folder = -1;
            }
        }
        mw_paint_window_client(shell_handle);
        return;
    }

    if (ty >= TASKBAR_Y && tx >= START_X && tx < START_X + START_W) {
        smenu_open = true;
        mw_paint_window_client(shell_handle);
        return;
    }

    // Taskbar app button tap
    {
        int n = taskbar_entry_count;
        int16_t area_x = (int16_t)(START_X + START_W + 6);
        int16_t area_w = (int16_t)((SCR_W - 52) - area_x);
        if (n > 0 && ty >= TASKBAR_Y && area_w >= 22 &&
            tx >= area_x && tx < (int16_t)(area_x + area_w)) {
            int16_t pitch = (int16_t)(area_w / n);
            int idx = (tx - area_x) / pitch;
            if (idx >= 0 && idx < n) {
                mw_handle_t h = taskbar_entries[idx].handle;
                bool is_min     = (mw_get_window_flags(h) & MW_WINDOW_FLAG_IS_MINIMISED) != 0;
                bool is_focused = (h == taskbar_focused_handle) && !is_min;
                if (is_focused) {
                    mw_set_window_minimised(h, true);
                    taskbar_set_focus(MW_INVALID_HANDLE);
                } else {
                    if (is_min) mw_set_window_minimised(h, false);
                    taskbar_set_focus(h);
                    mw_bring_window_to_front(h);
                }
                mw_paint_all();
            }
        }
    }
}

extern "C" {

void mw_user_init(void)
{
    hal_input_init();

    // Seed identity calibration matrix for GT911 (capacitive — no interactive calibration).
    // Must run here (after kitt.init() initializes NVS), not in mw_hal_touch_init().
    mw_settings_load();
    if (!mw_settings_is_initialised() || !mw_settings_is_calibrated()) {
        MATRIX_CAL identity = {};
        identity.Divider = 4096;
        identity.An = (int32_t)mw_hal_lcd_get_display_width();
        identity.En = (int32_t)mw_hal_lcd_get_display_height();
        mw_settings_set_to_defaults();
        mw_settings_set_calibration_matrix(&identity);
        mw_settings_set_calibrated(true);
        mw_settings_save();
        ESP_LOGI("purr_app", "GT911 identity calibration seeded");
    }

    mw_util_rect_t r;
    mw_util_set_rect(&r, 0, 0, SCR_W, SCR_H);
    shell_handle = mw_add_window(&r, "",
        shell_paint, shell_message, NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT,
        NULL);

    // Initialize cursor window (always-on-top, fullscreen)
    app_cursor_init();

    mw_paint_all();
}

void mw_user_root_paint_function(const mw_gl_draw_info_t *draw_info)
{
    // Cursor is now drawn by app_cursor fullscreen window (always-on-top)
    (void)draw_info;
}

void mw_user_root_message_function(const mw_message_t *message)
{
    (void)message;
}

} // extern "C"
