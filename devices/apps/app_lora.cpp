#include "app_lora.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "kitt.h"
#include <stdio.h>
#include <string.h>
#include "purr_apps_common.h"
#include "purr_taskbar.h"

extern KITT kitt;

static mw_handle_t s_handle    = MW_INVALID_HANDLE;
static char s_last_rx[24]      = "none";

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    char buf[32];

    if (!kitt.lora_enabled()) {
        mw_gl_string(d, 8,  8, "LoRa: not available");
        mw_gl_string(d, 8, 22, "Enable PURR_ENABLE_LORA");
        mw_gl_string(d, 8, 36, "in build config.");
        return;
    }

    snprintf(buf, sizeof(buf), "Freq:  %luMHz",
             (unsigned long)(kitt.lora_get_frequency() / 1000000));
    mw_gl_string(d, 8, 8, buf);
    snprintf(buf, sizeof(buf), "Power: %ddBm",
             (int)kitt.lora_get_power());
    mw_gl_string(d, 8, 20, buf);

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 4, cr.width - 4, 32);
    mw_gl_set_fg_colour(WCE_TXT);

    snprintf(buf, sizeof(buf), "RSSI: %ddBm   SNR: %.1f",
             kitt.lora_get_rssi(), (double)kitt.lora_get_snr());
    mw_gl_string(d, 8, 40, buf);

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 4, cr.width - 4, 52);
    mw_gl_set_fg_colour(WCE_TXT);

    mw_gl_string(d, 8, 60, "Last RX:");
    mw_gl_string(d, 8, 72, s_last_rx);
    mw_gl_string(d, 8, 86, kitt.lora_busy() ? "TX in progress" : "Idle");
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        /* fall through */
    case MW_TIMER_MESSAGE:
        if (kitt.lora_data_available()) {
            uint8_t buf[24];
            size_t n = kitt.lora_read(buf, sizeof(buf) - 1);
            buf[n] = 0;
            strncpy(s_last_rx, (const char *)buf, sizeof(s_last_rx) - 1);
        }
        mw_paint_window_client(msg->recipient_handle);
        mw_set_timer(MW_TICKS_PER_SECOND, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
        break;
    default:
        break;
    }
}

void app_lora_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        return;
    }
    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(200), 30, 200, 130);
    s_handle = mw_add_window(&r, "LoRa",
        paint, message, NULL, 0, APP_WIN_FLAGS, NULL);
    taskbar_register(s_handle, "LoRa");
}
