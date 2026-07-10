// settings.c — PURR OS system settings app (.claw)
// Uses purr_win.h — compatible with KittenUI (LVGL) and MiniWin.
//
// Sections: Theme  |  Display  |  Storage  |  Input
// Settings are persisted to NVS under namespace "purr_settings".

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"
#include "wifi_mgr.h"
#include "bt_mgr.h"
#include "mesh_ble.h"
#include "sdkconfig.h"

#define NVS_NS  "purr_settings"

#define MAX_WALLPAPERS      16
// Generous enough to comfortably hold "/sdcard/wallpapers/" + a max-length
// filesystem filename with room to spare — GCC's -Wformat-truncation (an
// error under this build's -Werror) can otherwise flag the directory-listing
// snprintf() below as a possible truncation.
#define WALLPAPER_PATH_LEN  300

// ── State ─────────────────────────────────────────────────────────────────────

static purr_win_t  s_win       = 0;
static purr_wid_t  s_status    = 0;   // bottom status label
static purr_wid_t  s_brightness_lbl = 0;

static uint8_t     s_brightness = 255;
static char        s_theme[16]  = "wce";

static purr_wid_t  s_kb_backlight_lbl = 0;
static uint8_t     s_kb_backlight = 0;

static purr_wid_t  s_dev_mode_lbl = 0;
static uint8_t     s_dev_mode     = 0;   // 0/1 — see purr_kernel.h's doc comment

static purr_wid_t  s_about_lbl = 0;

#define MAX_WIFI_RESULTS 24
static purr_win_t  s_wifi_win        = 0;   // separate "WiFi Settings" window, built on demand
static purr_wid_t  s_wifi_status_lbl = 0;
static purr_wid_t  s_wifi_list       = 0;
static char        s_wifi_labels[MAX_WIFI_RESULTS][64];
static const char *s_wifi_label_ptrs[MAX_WIFI_RESULTS];
static char        s_wifi_ssids[MAX_WIFI_RESULTS][33];   // parallel to the list above
static bool        s_wifi_secured[MAX_WIFI_RESULTS];
static int         s_wifi_count = 0;

static purr_win_t  s_wifi_dlg_win   = 0;
static purr_wid_t  s_wifi_dlg_input = 0;
static char        s_wifi_dlg_ssid[33] = "";

// Bluetooth is gated behind CONFIG_BT_NIMBLE_ENABLED (off by default — see
// bt_mgr.c/Kconfig.projbuild) — the whole section, state included, compiles
// out when it's off rather than being deleted, so a future device that
// enables it gets the full working UI back with zero further changes.
#ifdef CONFIG_BT_NIMBLE_ENABLED
#define MAX_BT_RESULTS 24
static purr_win_t  s_bt_win        = 0;   // separate "Bluetooth Settings" window, built on demand
static purr_wid_t  s_bt_status_lbl = 0;
static purr_wid_t  s_bt_list       = 0;
static char        s_bt_labels[MAX_BT_RESULTS][48];
static const char *s_bt_label_ptrs[MAX_BT_RESULTS];
static uint8_t     s_bt_addrs[MAX_BT_RESULTS][6];
static int         s_bt_count = 0;
#endif  // CONFIG_BT_NIMBLE_ENABLED

static purr_wid_t  s_wallpaper_list = 0;
static char        s_wallpaper_paths[MAX_WALLPAPERS][WALLPAPER_PATH_LEN];
static char        s_wallpaper_labels[MAX_WALLPAPERS][40];
static const char *s_wallpaper_label_ptrs[MAX_WALLPAPERS];
static int         s_wallpaper_count = 0;

// ── NVS helpers ───────────────────────────────────────────────────────────────

static void nvs_save_str(const char *key, const char *val) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, val);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_u8(const char *key, uint8_t val) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, val);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_load(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_theme);
    nvs_get_str(h, "theme", s_theme, &len);
    nvs_get_u8(h, "brightness", &s_brightness);
    nvs_get_u8(h, "kb_backlight", &s_kb_backlight);
    nvs_get_u8(h, "dev_mode", &s_dev_mode);
    nvs_close(h);
}

static void set_status(const char *msg) {
    purr_win_label_set(s_status, msg);
}

// ── Theme buttons ─────────────────────────────────────────────────────────────

static void apply_theme_nvs(const char *id) {
    nvs_save_str("theme", id);
    strncpy(s_theme, id, sizeof(s_theme) - 1);

    char msg[48];
    snprintf(msg, sizeof(msg), "Theme set to '%s' — reboot to apply.", id);
    set_status(msg);
}

static void on_theme_wce(purr_wid_t w, purr_event_t e, void *u)  { (void)w;(void)e;(void)u; apply_theme_nvs("wce");  }
static void on_theme_dark(purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; apply_theme_nvs("dark"); }

// ── Brightness ────────────────────────────────────────────────────────────────

static void set_brightness(uint8_t level) {
    s_brightness = level;
    const catcall_display_t *disp = purr_kernel_display();
    if (disp && disp->set_brightness) disp->set_brightness(level);
    nvs_save_u8("brightness", level);
    char buf[32];
    snprintf(buf, sizeof(buf), "Brightness: %d%%", (level * 100) / 255);
    purr_win_label_set(s_brightness_lbl, buf);
    set_status("Brightness updated.");
}

static void on_bright_high(purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; set_brightness(255); }
static void on_bright_mid (purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; set_brightness(160); }
static void on_bright_low (purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; set_brightness(80);  }

// ── Keyboard backlight ────────────────────────────────────────────────────────
// Goes through purr_kernel_keyboard_set_backlight() (dispatches to whichever
// registered input driver implements set_backlight — bbq20 here) rather than
// calling the bbq20 driver directly, mirroring set_brightness() above going
// through catcall_display instead of st7789.c directly.

static void set_kb_backlight(uint8_t level) {
    s_kb_backlight = level;
    purr_kernel_keyboard_set_backlight(level);
    nvs_save_u8("kb_backlight", level);
    char buf[32];
    snprintf(buf, sizeof(buf), "Kbd backlight: %d%%", (level * 100) / 255);
    purr_win_label_set(s_kb_backlight_lbl, buf);
    set_status("Keyboard backlight updated.");
}

static void on_kb_bl_off (purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; set_kb_backlight(0);   }
static void on_kb_bl_low (purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; set_kb_backlight(80);  }
static void on_kb_bl_mid (purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; set_kb_backlight(160); }
static void on_kb_bl_high(purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; set_kb_backlight(255); }

// ── Wallpaper ─────────────────────────────────────────────────────────────────
// "Default" (the launcher's built-in gradient) plus every file found under
// /sdcard/wallpapers/ — same opendir/readdir listing pattern fileman.c already
// uses. Selecting one just persists the choice to NVS; the launcher reads it
// back and loads the image itself on next boot (matching the existing
// theme/brightness "reboot to apply" behavior below).

static void refresh_wallpaper_list(void) {
    s_wallpaper_count = 0;

    strncpy(s_wallpaper_paths[s_wallpaper_count], "default", WALLPAPER_PATH_LEN - 1);
    strncpy(s_wallpaper_labels[s_wallpaper_count], "Default", sizeof(s_wallpaper_labels[0]) - 1);
    s_wallpaper_count++;

    DIR *d = opendir("/sdcard/wallpapers");
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL && s_wallpaper_count < MAX_WALLPAPERS) {
            if (ent->d_name[0] == '.') continue;
            snprintf(s_wallpaper_paths[s_wallpaper_count], WALLPAPER_PATH_LEN,
                     "/sdcard/wallpapers/%s", ent->d_name);
            strncpy(s_wallpaper_labels[s_wallpaper_count], ent->d_name,
                    sizeof(s_wallpaper_labels[0]) - 1);
            s_wallpaper_count++;
        }
        closedir(d);
    }

    for (int i = 0; i < s_wallpaper_count; i++) s_wallpaper_label_ptrs[i] = s_wallpaper_labels[i];
    purr_win_list_set_items(s_wallpaper_list, s_wallpaper_label_ptrs, s_wallpaper_count);
}

static void on_wallpaper_select(purr_wid_t w, purr_event_t e, void *u) {
    (void)w; (void)u;
    if (e != PURR_EVENT_ACTIVATED) return;
    int idx = purr_win_list_get_selected(s_wallpaper_list);
    if (idx < 0 || idx >= s_wallpaper_count) return;

    nvs_save_str("wallpaper", s_wallpaper_paths[idx]);
    char msg[WALLPAPER_PATH_LEN + 64];
    snprintf(msg, sizeof(msg), "Wallpaper set to '%s' — reboot to apply.", s_wallpaper_paths[idx]);
    set_status(msg);
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
// Lives in its own window (opened from the "WiFi Settings" button on the main
// Settings screen) rather than as an inline section — keeps the main window's
// widget count small (MiniWin's control/message-queue budget is finite) and
// gives WiFi its own focused space to connect/disconnect.

static void set_wifi_status(const char *msg) {
    if (s_wifi_status_lbl) purr_win_label_set(s_wifi_status_lbl, msg);
}

static void refresh_wifi_status(void) {
    switch (wifi_mgr_status()) {
        case WIFI_MGR_CONNECTED:
            { char buf[48]; snprintf(buf, sizeof(buf), "WiFi: connected (%s)", wifi_mgr_ip_str());
              set_wifi_status(buf); }
            break;
        case WIFI_MGR_CONNECTING: set_wifi_status("WiFi: connecting..."); break;
        case WIFI_MGR_FAILED:     set_wifi_status("WiFi: connection failed."); break;
        default:                  set_wifi_status("WiFi: idle."); break;
    }
}

static void close_wifi_dialog(void) {
    if (s_wifi_dlg_win) purr_win_destroy(s_wifi_dlg_win);
    s_wifi_dlg_win = 0;
    s_wifi_dlg_input = 0;
}

static void on_wifi_dlg_cancel(purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; close_wifi_dialog(); }

static void on_wifi_dlg_connect(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    const char *password = s_wifi_dlg_input ? purr_win_textarea_get(s_wifi_dlg_input) : "";
    wifi_mgr_connect(s_wifi_dlg_ssid, password ? password : "");
    close_wifi_dialog();
    refresh_wifi_status();
}

static void on_wifi_select(purr_wid_t w, purr_event_t e, void *u) {
    (void)w; (void)u;
    if (e != PURR_EVENT_ACTIVATED) return;
    int idx = purr_win_list_get_selected(s_wifi_list);
    if (idx < 0 || idx >= s_wifi_count) return;
    strncpy(s_wifi_dlg_ssid, s_wifi_ssids[idx], sizeof(s_wifi_dlg_ssid) - 1);

    // Open network — connect straight away, no password prompt needed.
    if (!s_wifi_secured[idx]) {
        wifi_mgr_connect(s_wifi_dlg_ssid, "");
        refresh_wifi_status();
        return;
    }

    s_wifi_dlg_win = purr_win_create("WiFi Password");
    char lbl[48];
    snprintf(lbl, sizeof(lbl), "Password for %s:", s_wifi_dlg_ssid);
    purr_win_label(s_wifi_dlg_win, lbl);
    s_wifi_dlg_input = purr_win_textarea(s_wifi_dlg_win, 90, 20);

    purr_wid_t row = purr_win_row(s_wifi_dlg_win, 4);
    purr_win_button(s_wifi_dlg_win, "Connect", on_wifi_dlg_connect, NULL);
    purr_win_button(s_wifi_dlg_win, "Cancel",  on_wifi_dlg_cancel,  NULL);
    purr_win_layout_end(row);

    purr_win_textarea_focus(s_wifi_dlg_input);
    // win_show() first — see terminal.c's terminal_init() for why (Cupcake's
    // win_show() raises the window above whatever kb_show() just showed).
    purr_win_show(s_wifi_dlg_win);
    purr_win_keyboard_show(s_wifi_dlg_win, s_wifi_dlg_input);
}

static void on_wifi_scan(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    set_status("Scanning...");
    int n = wifi_mgr_scan();
    if (n < 0) { set_status("WiFi scan failed."); return; }
    if (n > MAX_WIFI_RESULTS) n = MAX_WIFI_RESULTS;
    s_wifi_count = n;

    for (int i = 0; i < n; i++) {
        wifi_scan_result_t r;
        wifi_mgr_scan_at(i, &r);
        strncpy(s_wifi_ssids[i], r.ssid, sizeof(s_wifi_ssids[i]) - 1);
        s_wifi_secured[i] = r.secured;
        snprintf(s_wifi_labels[i], sizeof(s_wifi_labels[i]), "%s  %ddBm  (%s)",
                 r.ssid, r.rssi, r.secured ? "secured" : "open");
        s_wifi_label_ptrs[i] = s_wifi_labels[i];
    }
    purr_win_list_set_items(s_wifi_list, s_wifi_label_ptrs, s_wifi_count);
    set_status("Scan complete — tap a network to connect.");
}

static void on_wifi_disconnect(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    wifi_mgr_disconnect();
    refresh_wifi_status();
}

static void on_wifi_win_close(purr_wid_t win, purr_event_t event, void *u) {
    (void)win; (void)event; (void)u;
    close_wifi_dialog();
    s_wifi_win = 0;
    s_wifi_status_lbl = 0;
    s_wifi_list = 0;
}

static void on_wifi_settings_open(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    if (s_wifi_win) { purr_win_show(s_wifi_win); return; }

    s_wifi_win = purr_win_create("WiFi Settings");
    purr_win_on_close(s_wifi_win, on_wifi_win_close, NULL);

    purr_win_label(s_wifi_win, "Networks");
    s_wifi_list = purr_win_list(s_wifi_win, 90, 60);
    purr_win_list_on_select(s_wifi_list, on_wifi_select, NULL);

    purr_wid_t wr = purr_win_row(s_wifi_win, 4);
    purr_win_button(s_wifi_win, "Scan",       on_wifi_scan,       NULL);
    purr_win_button(s_wifi_win, "Disconnect", on_wifi_disconnect, NULL);
    purr_win_layout_end(wr);

    s_wifi_status_lbl = purr_win_label(s_wifi_win, "Ready.");
    refresh_wifi_status();
    purr_win_show(s_wifi_win);
}

// ── Bluetooth ─────────────────────────────────────────────────────────────────
// BLE only — T-Deck Plus's ESP32-S3 has no classic Bluetooth hardware (see
// bt_mgr.h's comment). Lives in its own window, same rationale as WiFi above.
// Gated behind CONFIG_BT_NIMBLE_ENABLED — see the s_bt_* state block above.
#ifdef CONFIG_BT_NIMBLE_ENABLED

static void set_bt_status(const char *msg) {
    if (s_bt_status_lbl) purr_win_label_set(s_bt_status_lbl, msg);
}

static void on_bt_toggle(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    bool want_on = !bt_mgr_is_enabled();
    bool on = bt_mgr_set_enabled(want_on);
    // bt_mgr_set_enabled() now lazily brings the NimBLE controller/host up
    // on its first enable — it can fail here (e.g. still no DMA-capable
    // memory available for some other reason) where it never used to be
    // able to at this point before (activation used to always happen at
    // boot instead). Only follow through on the Meshtastic companion
    // toggle if activation actually succeeded.
    if (want_on && !on) {
        set_bt_status("Bluetooth failed to start.");
        return;
    }
    mesh_ble_set_advertising(on);   // Meshtastic phone-app companion service follows the same toggle
    set_bt_status(on ? "Bluetooth enabled." : "Bluetooth disabled.");
}

static void on_bt_scan(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    if (!bt_mgr_is_enabled()) { set_bt_status("Enable Bluetooth first."); return; }
    set_bt_status("Scanning (BLE)...");
    int n = bt_mgr_scan(5);
    if (n < 0) { set_bt_status("Bluetooth scan failed."); return; }
    if (n > MAX_BT_RESULTS) n = MAX_BT_RESULTS;
    s_bt_count = n;

    for (int i = 0; i < n; i++) {
        bt_scan_result_t r;
        bt_mgr_scan_at(i, &r);
        memcpy(s_bt_addrs[i], r.addr, 6);
        snprintf(s_bt_labels[i], sizeof(s_bt_labels[i]), "%s  %ddBm", r.name, r.rssi);
        s_bt_label_ptrs[i] = s_bt_labels[i];
    }
    purr_win_list_set_items(s_bt_list, s_bt_label_ptrs, s_bt_count);
    set_bt_status("Scan complete — tap a device to pair.");
}

static void on_bt_select(purr_wid_t w, purr_event_t e, void *u) {
    (void)w; (void)u;
    if (e != PURR_EVENT_ACTIVATED) return;
    int idx = purr_win_list_get_selected(s_bt_list);
    if (idx < 0 || idx >= s_bt_count) return;

    esp_err_t ret = bt_mgr_pair(s_bt_addrs[idx]);
    char msg[64];
    snprintf(msg, sizeof(msg), "Pairing with %s...", s_bt_labels[idx]);
    set_bt_status(ret == ESP_OK ? msg : "Pair request failed.");
}

static void on_bt_win_close(purr_wid_t win, purr_event_t event, void *u) {
    (void)win; (void)event; (void)u;
    s_bt_win = 0;
    s_bt_status_lbl = 0;
    s_bt_list = 0;
}

static void on_bt_settings_open(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    if (s_bt_win) { purr_win_show(s_bt_win); return; }

    s_bt_win = purr_win_create("Bluetooth Settings");
    purr_win_on_close(s_bt_win, on_bt_win_close, NULL);

    purr_win_label(s_bt_win, "Devices (BLE)");
    s_bt_list = purr_win_list(s_bt_win, 90, 60);
    purr_win_list_on_select(s_bt_list, on_bt_select, NULL);

    purr_wid_t btr = purr_win_row(s_bt_win, 4);
    purr_win_button(s_bt_win, "Enable/Disable", on_bt_toggle, NULL);
    purr_win_button(s_bt_win, "Scan",           on_bt_scan,   NULL);
    purr_win_layout_end(btr);

    s_bt_status_lbl = purr_win_label(s_bt_win, bt_mgr_is_enabled() ? "Bluetooth enabled." : "Bluetooth disabled.");
    purr_win_show(s_bt_win);
}

#endif  // CONFIG_BT_NIMBLE_ENABLED

// ── Developer Mode ────────────────────────────────────────────────────────────
// Gates whether unsigned .hiss scripts are allowed to run — see
// purr_kernel_set_dev_mode()'s doc comment in purr_kernel.h. Only "unsigned"
// .hiss scripts are affected; a signed one always runs regardless of this.
// Off by default — persisted so it survives a reboot, same as brightness/
// kb_backlight above.

static void on_dev_mode_toggle(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    s_dev_mode = s_dev_mode ? 0 : 1;
    purr_kernel_set_dev_mode(s_dev_mode != 0);
    nvs_save_u8("dev_mode", s_dev_mode);
    purr_win_label_set(s_dev_mode_lbl, s_dev_mode ? "Developer Mode: ON" : "Developer Mode: OFF");
    set_status(s_dev_mode ? "Developer Mode enabled — unsigned .hiss scripts allowed."
                           : "Developer Mode disabled — unsigned .hiss scripts blocked.");
}

// ── Storage ───────────────────────────────────────────────────────────────────

static void on_sd_refresh(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    set_status(purr_kernel_sd_available() ? "SD card: present." : "SD card: not mounted.");
}

// ── Reboot ────────────────────────────────────────────────────────────────────

static void on_reboot(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    set_status("Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    purr_kernel_reboot();
}

// ── About ─────────────────────────────────────────────────────────────────────
// Absorbed from the standalone about.c app — Settings is already a persistent
// window, so unlike about.c this is a one-shot fill on open rather than a
// periodic-updater task; a settings screen doesn't need live-ticking
// uptime/RAM. Content mirrors about.c's build_info() minus the box-drawing
// header, kept plain to match this app's other section labels.

static void build_about_text(char *buf, size_t sz) {
    size_t pos = 0;
#define APPEND(fmt, ...) \
    do { pos += snprintf(buf + pos, sz - pos, fmt "\n", ##__VA_ARGS__); } while(0)

    APPEND("PURR OS v%s  (KITT v%s)", PURR_KERNEL_VERSION, KITT_VERSION);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const char *model = "ESP32";
    if      (chip.model == CHIP_ESP32S3) model = "ESP32-S3";
    else if (chip.model == CHIP_ESP32S2) model = "ESP32-S2";
    else if (chip.model == CHIP_ESP32C3) model = "ESP32-C3";
    APPEND("Chip: %s r%d  %d cores", model, chip.revision, chip.cores);

    uint32_t flash_sz = 0;
    esp_flash_get_size(NULL, &flash_sz);
    APPEND("Flash: %lu MB   Free RAM: %lu KB",
           (unsigned long)(flash_sz / (1024 * 1024)),
           (unsigned long)(purr_kernel_free_ram() / 1024));

    uint64_t up_s = purr_kernel_uptime_ms() / 1000ULL;
    APPEND("Uptime: %02luh %02lum %02lus",
           (unsigned long)(up_s / 3600), (unsigned long)((up_s % 3600) / 60),
           (unsigned long)(up_s % 60));

    const catcall_display_t *disp  = purr_kernel_display();
    const catcall_touch_t   *touch = purr_kernel_touch();
    const catcall_input_t   *input = purr_kernel_input();
    const catcall_radio_t   *radio = purr_kernel_radio();
    const catcall_gps_t     *gps   = purr_kernel_gps();
    const catcall_ui_t      *ui    = purr_kernel_ui();
    APPEND("Display:%s Touch:%s Input:%s", disp ? disp->name : "-", touch ? touch->name : "-", input ? input->name : "-");
    APPEND("Radio:%s GPS:%s UI:%s", radio ? radio->name : "-", gps ? gps->name : "-", ui ? ui->name : "-");
    APPEND("SD card: %s", purr_kernel_sd_available() ? "mounted" : "not mounted");

#undef APPEND
}

// ── Build UI ──────────────────────────────────────────────────────────────────

static int settings_init(void) {
    nvs_load();
    purr_kernel_set_dev_mode(s_dev_mode != 0);

    s_win = purr_win_create("Settings");

    // ── Theme section ──────────────────────────────────────────────────────
    purr_win_label(s_win, "Theme");
    purr_wid_t tr = purr_win_row(s_win, 4);
    purr_win_button(s_win, "WCE Classic", on_theme_wce,  NULL);
    purr_win_button(s_win, "Dark",        on_theme_dark, NULL);
    purr_win_layout_end(tr);

    char theme_str[40];
    snprintf(theme_str, sizeof(theme_str), "Active: %s", s_theme);
    purr_win_label(s_win, theme_str);

    // ── Brightness section ─────────────────────────────────────────────────
    purr_win_label(s_win, "Display");
    char bright_str[32];
    snprintf(bright_str, sizeof(bright_str), "Brightness: %d%%", (s_brightness * 100) / 255);
    s_brightness_lbl = purr_win_label(s_win, bright_str);

    purr_wid_t br = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Low",  on_bright_low,  NULL);
    purr_win_button(s_win, "Mid",  on_bright_mid,  NULL);
    purr_win_button(s_win, "High", on_bright_high, NULL);
    purr_win_layout_end(br);

    // ── Keyboard backlight section ─────────────────────────────────────────
    purr_win_label(s_win, "Keyboard Backlight");
    char kb_bl_str[32];
    snprintf(kb_bl_str, sizeof(kb_bl_str), "Kbd backlight: %d%%", (s_kb_backlight * 100) / 255);
    s_kb_backlight_lbl = purr_win_label(s_win, kb_bl_str);

    purr_wid_t kbr = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Off",  on_kb_bl_off,  NULL);
    purr_win_button(s_win, "Low",  on_kb_bl_low,  NULL);
    purr_win_button(s_win, "Mid",  on_kb_bl_mid,  NULL);
    purr_win_button(s_win, "High", on_kb_bl_high, NULL);
    purr_win_layout_end(kbr);
    purr_kernel_keyboard_set_backlight(s_kb_backlight);

    // ── Network section ─────────────────────────────────────────────────────
    // WiFi and Bluetooth each get their own dedicated window (opened here)
    // instead of being built inline — keeps this main window's widget count
    // small and gives each its own focused space.
    purr_win_label(s_win, "Network");
    purr_wid_t nr = purr_win_row(s_win, 4);
    purr_win_button(s_win, "WiFi Settings",      on_wifi_settings_open, NULL);
#ifdef CONFIG_BT_NIMBLE_ENABLED
    purr_win_button(s_win, "Bluetooth Settings", on_bt_settings_open,   NULL);
#endif
    purr_win_layout_end(nr);

    // ── Wallpaper section ──────────────────────────────────────────────────
    purr_win_label(s_win, "Wallpaper");
    s_wallpaper_list = purr_win_list(s_win, 90, 30);
    purr_win_list_on_select(s_wallpaper_list, on_wallpaper_select, NULL);
    refresh_wallpaper_list();

    // ── Storage section ────────────────────────────────────────────────────
    purr_win_label(s_win, "Storage");
    purr_wid_t sr = purr_win_row(s_win, 4);
    purr_win_button(s_win, "SD Status", on_sd_refresh, NULL);
    purr_win_layout_end(sr);

    // ── Developer section ──────────────────────────────────────────────────
    purr_win_label(s_win, "Developer");
    char dev_str[32];
    snprintf(dev_str, sizeof(dev_str), "Developer Mode: %s", s_dev_mode ? "ON" : "OFF");
    s_dev_mode_lbl = purr_win_label(s_win, dev_str);
    purr_wid_t devr = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Toggle", on_dev_mode_toggle, NULL);
    purr_win_layout_end(devr);

    // ── System section ─────────────────────────────────────────────────────
    purr_win_label(s_win, "System");
    purr_wid_t sys = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Reboot", on_reboot, NULL);
    purr_win_layout_end(sys);

    // ── About section ──────────────────────────────────────────────────────
    purr_win_label(s_win, "About");
    char about_buf[512];
    build_about_text(about_buf, sizeof(about_buf));
    s_about_lbl = purr_win_label(s_win, about_buf);

    // ── Status bar ─────────────────────────────────────────────────────────
    s_status = purr_win_label(s_win, "Ready.");

    purr_win_show(s_win);
    return 0;
}

static void settings_deinit(void) {
    close_wifi_dialog();
    if (s_wifi_win) { purr_win_destroy(s_wifi_win); s_wifi_win = 0; s_wifi_status_lbl = 0; s_wifi_list = 0; }
#ifdef CONFIG_BT_NIMBLE_ENABLED
    if (s_bt_win)   { purr_win_destroy(s_bt_win);   s_bt_win   = 0; s_bt_status_lbl   = 0; s_bt_list   = 0; }
#endif
    purr_win_destroy(s_win);
    s_win = 0;
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(settings) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "settings",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = settings_init,
    .deinit            = settings_deinit,
};
