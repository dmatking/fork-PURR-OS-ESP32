// Restart Menu App
// Choose boot mode and reboot: PURR OS or MagicMac

#include "app_restart_menu.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"
#include "../kernel/kitt.h"
#include <stdio.h>

static mw_handle_t s_handle = MW_INVALID_HANDLE;

enum {
    BUTTON_PURR_OS,
    BUTTON_MAGICMAC,
    BUTTON_CANCEL,
};

static int selected_button = BUTTON_PURR_OS;

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);

    // Background
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);

    // Title
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_string(d, 8, 8, "Restart Menu");

    // Separator
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 4, cr.width - 4, 20);

    // Button 1: PURR OS
    uint32_t color1 = (selected_button == BUTTON_PURR_OS) ? 0x00FF00 : WCE_TXT;
    mw_gl_set_fg_colour(color1);
    mw_gl_rectangle(d, 10, 30, 100, 40);
    mw_gl_set_fg_colour(WCE_BAR);
    mw_gl_rectangle(d, 12, 32, 96, 36);
    mw_gl_set_fg_colour(color1);
    mw_gl_string(d, 20, 35, "PURR OS");

    // Button 2: MagicMac
    uint32_t color2 = (selected_button == BUTTON_MAGICMAC) ? 0x00FF00 : WCE_TXT;
    mw_gl_set_fg_colour(color2);
    mw_gl_rectangle(d, 120, 30, 210, 40);
    mw_gl_set_fg_colour(WCE_BAR);
    mw_gl_rectangle(d, 122, 32, 206, 36);
    mw_gl_set_fg_colour(color2);
    mw_gl_string(d, 130, 35, "MagicMac");

    // Button 3: Cancel
    uint32_t color3 = (selected_button == BUTTON_CANCEL) ? 0x00FF00 : WCE_TXT;
    mw_gl_set_fg_colour(color3);
    mw_gl_rectangle(d, 10, 80, 210, 90);
    mw_gl_set_fg_colour(WCE_BAR);
    mw_gl_rectangle(d, 12, 82, 206, 86);
    mw_gl_set_fg_colour(color3);
    mw_gl_string(d, 70, 83, "Cancel");

    // Instructions
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_set_font(MW_GL_FONT_8);
    mw_gl_string(d, 8, 110, "Select boot mode and restart,");
    mw_gl_string(d, 8, 120, "or cancel to return to desktop");
}

static void message(const mw_message_t *msg)
{
    extern KITT kitt;

    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        break;

    case MW_TOUCH_DOWN_MESSAGE: {
        int16_t x = msg->touch_down_x;
        int16_t y = msg->touch_down_y;

        // Button hit test (relative to window client area)
        mw_util_rect_t cr = mw_get_window_client_rect(msg->recipient_handle);
        int16_t rel_x = x - cr.x;
        int16_t rel_y = y - cr.y;

        // PURR OS button: 10-110, 30-40
        if (rel_x >= 10 && rel_x <= 110 && rel_y >= 30 && rel_y <= 40) {
            selected_button = BUTTON_PURR_OS;
            mw_paint_window_client(msg->recipient_handle);
        }
        // MagicMac button: 120-210, 30-40
        else if (rel_x >= 120 && rel_x <= 210 && rel_y >= 30 && rel_y <= 40) {
            selected_button = BUTTON_MAGICMAC;
            mw_paint_window_client(msg->recipient_handle);
        }
        // Cancel button: 10-210, 80-90
        else if (rel_x >= 10 && rel_x <= 210 && rel_y >= 80 && rel_y <= 90) {
            selected_button = BUTTON_CANCEL;
            mw_paint_window_client(msg->recipient_handle);
        }
        break;
    }

    case MW_TOUCH_UP_MESSAGE: {
        int16_t x = msg->touch_up_x;
        int16_t y = msg->touch_up_y;

        // Button hit test (relative to window client area)
        mw_util_rect_t cr = mw_get_window_client_rect(msg->recipient_handle);
        int16_t rel_x = x - cr.x;
        int16_t rel_y = y - cr.y;

        // PURR OS button clicked
        if (rel_x >= 10 && rel_x <= 110 && rel_y >= 30 && rel_y <= 40) {
            ESP_LOGI("restart_menu", "Restarting to PURR OS");
            kitt.set_boot_mode(BOOT_PURR_OS);
            vTaskDelay(pdMS_TO_TICKS(500));
            kitt.reboot();
        }
        // MagicMac button clicked
        else if (rel_x >= 120 && rel_x <= 210 && rel_y >= 30 && rel_y <= 40) {
            ESP_LOGI("restart_menu", "Restarting to MagicMac");
            kitt.set_boot_mode(BOOT_MAGICMAC);
            vTaskDelay(pdMS_TO_TICKS(500));
            kitt.reboot();
        }
        // Cancel button clicked
        else if (rel_x >= 10 && rel_x <= 210 && rel_y >= 80 && rel_y <= 90) {
            // Close window
            mw_remove_window(msg->recipient_handle);
        }
        break;
    }

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
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
    mw_util_set_rect(&r, 20, 50, 220, 140);
    s_handle = mw_add_window(&r, "Restart",
        paint, message, NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
    taskbar_register(s_handle, "Restart");
}
