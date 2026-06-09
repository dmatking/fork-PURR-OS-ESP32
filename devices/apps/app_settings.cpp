#include "app_settings.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "kitt.h"
#include "purr_version.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "purr_log.h"
#include <stdio.h>
#include <string.h>
#include "purr_apps_common.h"
#include "purr_taskbar.h"

extern KITT kitt;

typedef enum { TAB_SYS, TAB_WIFI, TAB_LORA, TAB_LOG, TAB_COUNT } tab_t;
typedef enum { WSCAN_IDLE, WSCAN_SCANNING, WSCAN_RESULTS } wscan_t;

static mw_handle_t s_handle    = MW_INVALID_HANDLE;
static tab_t       s_tab       = TAB_SYS;
static wscan_t     s_wifi      = WSCAN_IDLE;
static uint8_t     s_wanim     = 0;

#define WIN_W   240
#define WIN_H   198
#define TAB_H   16
#define TAB_W   (WIN_W / TAB_COUNT)

static const char *const tab_labels[TAB_COUNT] = { "Sys", "WiFi", "LoRa", "Log" };
static const char *const scan_anim[3] = { "Scanning.  ", "Scanning.. ", "Scanning..." };

static void paint_tab_bar(const mw_gl_draw_info_t *d)
{
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);

    for (int i = 0; i < TAB_COUNT; i++) {
        int16_t tx = (int16_t)(i * TAB_W);
        mw_gl_set_solid_fill_colour(WCE_BAR);
        mw_gl_rectangle(d, tx, 0, TAB_W, TAB_H);
        if (i == (int)s_tab) {
            mw_gl_set_fg_colour(WCE_DARK);
            mw_gl_hline(d, tx, (int16_t)(tx + TAB_W - 1), 0);
            mw_gl_vline(d, tx, 0, (int16_t)(TAB_H - 1));
            mw_gl_set_fg_colour(WCE_HI);
        } else {
            mw_gl_set_fg_colour(WCE_HI);
            mw_gl_hline(d, tx, (int16_t)(tx + TAB_W - 1), 0);
            mw_gl_vline(d, tx, 0, (int16_t)(TAB_H - 1));
            mw_gl_set_fg_colour(WCE_DARK);
        }
        mw_gl_hline(d, tx, (int16_t)(tx + TAB_W - 1), (int16_t)(TAB_H - 1));
        mw_gl_vline(d, (int16_t)(tx + TAB_W - 1), 0, (int16_t)(TAB_H - 1));
        mw_gl_set_fg_colour(WCE_TXT);
        mw_gl_string(d, (int16_t)(tx + 4), 4, tab_labels[i]);
    }
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 0, WIN_W - 1, TAB_H);
}

static void paint_sys(const mw_gl_draw_info_t *d, int16_t y0)
{
    char buf[40];
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);

    mw_gl_string(d, 4, (int16_t)(y0),
                 "PURR OS " PURR_OS_VERSION "  KITT " KITT_VERSION);
    mw_gl_string(d, 4, (int16_t)(y0 + 12), kitt.device_name());
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 2, WIN_W - 2, (int16_t)(y0 + 24));
    mw_gl_set_fg_colour(WCE_TXT);

    unsigned free_kb  = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT)  / 1024);
    unsigned total_kb = (unsigned)(heap_caps_get_total_size(MALLOC_CAP_8BIT) / 1024);
    snprintf(buf, sizeof(buf), "RAM: %ukB free / %ukB tot", free_kb, total_kb);
    mw_gl_string(d, 4, (int16_t)(y0 + 30), buf);

    snprintf(buf, sizeof(buf), "CPU: %dMHz", kitt.cpu_get_freq_mhz());
    mw_gl_string(d, 4, (int16_t)(y0 + 42), buf);

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 2, WIN_W - 2, (int16_t)(y0 + 54));
    mw_gl_set_fg_colour(WCE_TXT);

    int batt = kitt.battery_percent();
    if (batt > 0) {
        snprintf(buf, sizeof(buf), "Battery: %d%%  %dmV",
                 batt, kitt.battery_voltage_mv());
        mw_gl_string(d, 4, (int16_t)(y0 + 60), buf);
    }

    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    int16_t row = (int16_t)(y0 + (batt > 0 ? 72 : 60));
    snprintf(buf, sizeof(buf), "Uptime: %lus", (unsigned long)uptime_s);
    mw_gl_string(d, 4, row, buf);
    snprintf(buf, sizeof(buf), "SD: %s", kitt.sd_available() ? "ready" : "not mounted");
    mw_gl_string(d, 4, (int16_t)(row + 12), buf);
}

static void paint_wifi(const mw_gl_draw_info_t *d, int16_t y0)
{
    char buf[36];
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);

    if (s_wifi == WSCAN_SCANNING) {
        mw_gl_string(d, 4, y0, scan_anim[s_wanim % 3]);
        return;
    }

    if (!kitt.wifi_enabled()) {
        mw_gl_string(d, 4, y0, "WiFi: disabled");
    } else if (kitt.wifi_connected()) {
        char ssid[22] = {};
        kitt.wifi_get_connected_ssid(ssid, sizeof(ssid));
        snprintf(buf, sizeof(buf), "Connected: %.20s", ssid);
        mw_gl_string(d, 4, y0, buf);
        snprintf(buf, sizeof(buf), "RSSI: %ddBm", kitt.wifi_signal_strength());
        mw_gl_string(d, 4, (int16_t)(y0 + 12), buf);
    } else {
        mw_gl_string(d, 4, y0, "Not connected");
    }

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 2, WIN_W - 2, (int16_t)(y0 + 26));
    mw_gl_set_fg_colour(WCE_TXT);

    if (s_wifi == WSCAN_RESULTS) {
        int n = kitt.wifi_scan_count();
        mw_gl_string(d, 4, (int16_t)(y0 + 32),
                     n > 0 ? "Nearby networks:" : "No networks found");
        for (int i = 0; i < n && i < 7; i++) {
            char ssid[16] = {};
            kitt.wifi_scan_get_ssid(i, ssid, sizeof(ssid));
            snprintf(buf, sizeof(buf), "%-13.13s %4ddBm%s",
                     ssid, kitt.wifi_scan_get_rssi(i),
                     kitt.wifi_scan_get_secured(i) ? "*" : " ");
            mw_gl_string(d, 4, (int16_t)(y0 + 44 + i * 13), buf);
        }
    } else {
        mw_gl_string(d, 4, (int16_t)(y0 + 32), "Tap to scan");
    }
}

static void paint_lora(const mw_gl_draw_info_t *d, int16_t y0)
{
    char buf[32];
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);

    if (!kitt.lora_enabled()) {
        mw_gl_string(d, 4, y0,           "LoRa: not available");
        mw_gl_string(d, 4, (int16_t)(y0 + 14), "Enable PURR_ENABLE_LORA");
        mw_gl_string(d, 4, (int16_t)(y0 + 28), "in build config.");
        return;
    }

    snprintf(buf, sizeof(buf), "Freq:  %luMHz",
             (unsigned long)(kitt.lora_get_frequency() / 1000000));
    mw_gl_string(d, 4, y0, buf);
    snprintf(buf, sizeof(buf), "Power: %ddBm", (int)kitt.lora_get_power());
    mw_gl_string(d, 4, (int16_t)(y0 + 12), buf);

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 2, WIN_W - 2, (int16_t)(y0 + 24));
    mw_gl_set_fg_colour(WCE_TXT);

    snprintf(buf, sizeof(buf), "RSSI: %ddBm  SNR: %.1f",
             kitt.lora_get_rssi(), (double)kitt.lora_get_snr());
    mw_gl_string(d, 4, (int16_t)(y0 + 30), buf);
    mw_gl_string(d, 4, (int16_t)(y0 + 44),
                 kitt.lora_busy() ? "TX in progress" : "Idle");
}

static void paint_log(const mw_gl_draw_info_t *d, int16_t y0)
{
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);

    if (purr_log_count == 0) {
        mw_gl_string(d, 2, y0, "No log entries yet.");
        return;
    }

    int start = (purr_log_head - purr_log_count + PURR_LOG_LINES) % PURR_LOG_LINES;
    for (int i = 0; i < purr_log_count; i++) {
        int16_t ly = (int16_t)(y0 + i * 13);
        mw_gl_string(d, 2, ly, purr_log_ring[(start + i) % PURR_LOG_LINES]);
    }
}

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);

    paint_tab_bar(d);

    int16_t y0 = (int16_t)(TAB_H + 5);
    switch (s_tab) {
    case TAB_SYS:  paint_sys(d, y0);  break;
    case TAB_WIFI: paint_wifi(d, y0); break;
    case TAB_LORA: paint_lora(d, y0); break;
    case TAB_LOG:  paint_log(d, y0);  break;
    default: break;
    }
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        purr_log_hook_install();
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        /* fall through */
    case MW_TIMER_MESSAGE:
        if (s_wifi == WSCAN_SCANNING) {
            s_wanim++;
            if (kitt.wifi_scan_done()) s_wifi = WSCAN_RESULTS;
        }
        mw_paint_window_client(msg->recipient_handle);
        mw_set_timer(MW_TICKS_PER_SECOND, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;

    case MW_TOUCH_DOWN_MESSAGE: {
        int16_t cx = (int16_t)(msg->message_data >> 16);
        int16_t cy = (int16_t)(msg->message_data & 0xFFFF);
        if (cy >= 0 && cy < TAB_H) {
            int new_tab = cx / TAB_W;
            if (new_tab >= 0 && new_tab < TAB_COUNT)
                s_tab = (tab_t)new_tab;
        } else if (s_tab == TAB_WIFI && cy >= TAB_H) {
            if (s_wifi != WSCAN_SCANNING && kitt.wifi_enabled()) {
                s_wifi  = WSCAN_SCANNING;
                s_wanim = 0;
                kitt.wifi_scan_start();
            }
        }
        mw_paint_window_client(msg->recipient_handle);
        break;
    }

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
        s_wifi   = WSCAN_IDLE;
        break;

    default:
        break;
    }
}

void app_settings_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        return;
    }
    s_tab  = TAB_SYS;
    s_wifi = WSCAN_IDLE;
    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(WIN_W), 10, WIN_W, WIN_H);
    s_handle = mw_add_window(&r, "Settings",
        paint, message, NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
    taskbar_register(s_handle, "Settings");
}
