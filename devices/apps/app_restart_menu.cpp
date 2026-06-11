// Restart / Boot menu — uses proper MiniWin button controls
// Three buttons: PURR OS, MagicMac, Cancel
// Arrow keys (trackball) navigate focus, click/enter activates

#include "app_restart_menu.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "ui/ui_button.h"
#include "gl/gl.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"
#include "../kernel/kitt.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static mw_handle_t s_handle = MW_INVALID_HANDLE;

static mw_handle_t s_btn_purros   = MW_INVALID_HANDLE;
static mw_handle_t s_btn_magicmac = MW_INVALID_HANDLE;
static mw_handle_t s_btn_cancel   = MW_INVALID_HANDLE;

static mw_ui_button_data_t s_data_purros   = { "PURR OS",   false };
static mw_ui_button_data_t s_data_magicmac = { "MagicMac",  false };
static mw_ui_button_data_t s_data_cancel   = { "Cancel",    false };

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);

    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);

    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_string(d, 8, 6, "Restart / Boot Mode");

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 4, cr.width - 4, 20);

    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, 8, 24, "Select and press Enter (trackball click):");
}

static void message(const mw_message_t *msg)
{
    extern KITT kitt;

    switch (msg->message_id) {

    case MW_WINDOW_CREATED_MESSAGE:
        // Create the three buttons using proper MiniWin API
        s_btn_purros = mw_ui_button_add_new(
            8, 36, s_handle,
            MW_CONTROL_FLAG_IS_VISIBLE | MW_CONTROL_FLAG_IS_ENABLED | MW_CONTROL_FLAG_LARGE_SIZE,
            &s_data_purros);

        s_btn_magicmac = mw_ui_button_add_new(
            8, 74, s_handle,
            MW_CONTROL_FLAG_IS_VISIBLE | MW_CONTROL_FLAG_IS_ENABLED | MW_CONTROL_FLAG_LARGE_SIZE,
            &s_data_magicmac);

        s_btn_cancel = mw_ui_button_add_new(
            8, 112, s_handle,
            MW_CONTROL_FLAG_IS_VISIBLE | MW_CONTROL_FLAG_IS_ENABLED | MW_CONTROL_FLAG_LARGE_SIZE,
            &s_data_cancel);

        mw_paint_window_frame(s_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(s_handle);
        break;

    case MW_BUTTON_PRESSED_MESSAGE:
        if (msg->sender_handle == s_btn_purros) {
            kitt.set_boot_mode(BOOT_PURR_OS);
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        } else if (msg->sender_handle == s_btn_magicmac) {
            kitt.set_boot_mode(BOOT_MAGICMAC);
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        } else if (msg->sender_handle == s_btn_cancel) {
            mw_remove_window(s_handle);
        }
        break;

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle      = MW_INVALID_HANDLE;
        s_btn_purros   = MW_INVALID_HANDLE;
        s_btn_magicmac = MW_INVALID_HANDLE;
        s_btn_cancel   = MW_INVALID_HANDLE;
        break;

    default:
        break;
    }
}

void app_restart_menu_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        mw_bring_window_to_front(s_handle);
        return;
    }
    mw_util_rect_t r;
    mw_util_set_rect(&r, 30, 30, MW_UI_BUTTON_LARGE_WIDTH + 16, 152);
    s_handle = mw_add_window(&r, "Restart",
        paint, message, NULL, 0, APP_WIN_FLAGS, NULL);
    taskbar_register(s_handle, "Restart");
}
