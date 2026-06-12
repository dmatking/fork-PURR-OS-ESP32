// MagicMac app — shows ROM/disk status, lets user boot into Mac Plus emulator
// Booting switches boot mode via NVS then restarts into the MagicMac partition

#include "app_magicmac.h"
#include "app_restart_menu.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>

static mw_handle_t s_handle = MW_INVALID_HANDLE;

#define ROM_PATH  "/sdcard/magicmac/mac.rom"
#define DISK_PATH "/sdcard/magicmac/meow.dsk"

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}



static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);

    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);

    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_string(d, 8, 6, "MagicMac — Mac Plus Emulator");

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 4, cr.width - 4, 20);

    bool rom_ok  = file_exists(ROM_PATH);
    bool disk_ok = file_exists(DISK_PATH);

    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_set_fg_colour(rom_ok ? 0x00AA00 : 0xCC0000);
    mw_gl_string(d, 8, 26, rom_ok  ? "ROM:  mac.rom found"  : "ROM:  mac.rom MISSING");
    mw_gl_set_fg_colour(disk_ok ? 0x00AA00 : 0xCC0000);
    mw_gl_string(d, 8, 38, disk_ok ? "Disk: meow.dsk found" : "Disk: meow.dsk MISSING");
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_string(d, 8, 50, "Place files in /sdcard/magicmac/");

    mw_gl_hline(d, 4, cr.width - 4, 62);

    bool can_boot = rom_ok;
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_solid_fill_colour(can_boot ? 0x0066CC : 0x888888);
    mw_gl_rectangle(d, 8, 68, cr.width - 16, 18);
    mw_gl_set_fg_colour(MW_HAL_LCD_WHITE);
    mw_gl_string(d, 14, 72, can_boot ? "Boot MagicMac (restart)" : "Add ROM to SD first");
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        break;

    case MW_TOUCH_DOWN_MESSAGE: {
        int16_t tx = (int16_t)(msg->message_data >> 16);
        int16_t ty = (int16_t)(msg->message_data & 0xFFFF);
        mw_util_rect_t cr = mw_get_window_client_rect(msg->recipient_handle);
        int16_t rx = tx - cr.x;
        int16_t ry = ty - cr.y;
        // Boot button
        if (rx >= 8 && rx <= cr.width - 8 && ry >= 68 && ry <= 86) {
            if (file_exists(ROM_PATH))
                app_restart_menu_launch();
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

void app_magicmac_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        return;
    }
    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(240), 40, 240, 100);
    s_handle = mw_add_window(&r, "MagicMac",
        paint, message, NULL, 0, APP_WIN_FLAGS, NULL);
    taskbar_register(s_handle, "MagicMac");
}
