#include "app_wifi.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "kitt.h"
#include <stdio.h>
#include <string.h>
#include "purr_apps_common.h"
#include "purr_taskbar.h"

extern KITT kitt;

typedef enum { WIFI_IDLE, WIFI_SCANNING, WIFI_RESULTS } wifi_state_t;

static mw_handle_t s_handle      = MW_INVALID_HANDLE;
static wifi_state_t s_state      = WIFI_IDLE;
static uint8_t      s_anim_tick  = 0;

static const char *dot_anim[] = { "Scanning.  ", "Scanning.. ", "Scanning..." };

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    char buf[36];

    if (s_state == WIFI_SCANNING) {
        mw_gl_string(d, 8, 8, dot_anim[s_anim_tick % 3]);
        return;
    }

    // Connection status
    if (!kitt.wifi_enabled()) {
        mw_gl_string(d, 8, 8, "WiFi: disabled");
    } else if (kitt.wifi_connected()) {
        char ssid[22] = {};
        kitt.wifi_get_connected_ssid(ssid, sizeof(ssid));
        snprintf(buf, sizeof(buf), "Connected: %.20s", ssid);
        mw_gl_string(d, 8, 8, buf);
        snprintf(buf, sizeof(buf), "RSSI: %ddBm", kitt.wifi_signal_strength());
        mw_gl_string(d, 8, 20, buf);
    } else {
        mw_gl_string(d, 8, 8, "Not connected");
    }

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 4, cr.width - 4, 34);
    mw_gl_set_fg_colour(WCE_TXT);

    if (s_state == WIFI_RESULTS) {
        int n = kitt.wifi_scan_count();
        mw_gl_string(d, 8, 42, n > 0 ? "Nearby networks:" : "No networks found");
        for (int i = 0; i < n && i < 5; i++) {
            char ssid[18] = {};
            kitt.wifi_scan_get_ssid(i, ssid, sizeof(ssid));
            snprintf(buf, sizeof(buf), "%-15.15s %4ddBm%s",
                     ssid,
                     kitt.wifi_scan_get_rssi(i),
                     kitt.wifi_scan_get_secured(i) ? " *" : "  ");
            mw_gl_string(d, 8, (int16_t)(54 + i * 13), buf);
        }
    } else {
        mw_gl_string(d, 8, 42, "Tap to scan");
    }
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        s_state = WIFI_IDLE;
        /* fall through */
    case MW_TIMER_MESSAGE:
        if (s_state == WIFI_SCANNING) {
            s_anim_tick++;
            if (kitt.wifi_scan_done())
                s_state = WIFI_RESULTS;
        }
        mw_paint_window_client(msg->recipient_handle);
        mw_set_timer(MW_TICKS_PER_SECOND, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    case MW_TOUCH_DOWN_MESSAGE:
        if (s_state != WIFI_SCANNING && kitt.wifi_enabled()) {
            s_state      = WIFI_SCANNING;
            s_anim_tick  = 0;
            kitt.wifi_scan_start();
            mw_paint_window_client(msg->recipient_handle);
        }
        break;
    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
        s_state  = WIFI_IDLE;
        break;
    default:
        break;
    }
}

void app_wifi_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        return;
    }
    s_state = WIFI_IDLE;
    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(210), 25, 210, 155);
    s_handle = mw_add_window(&r, "WiFi",
        paint, message, NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
    taskbar_register(s_handle, "WiFi");
}
