// settings.c — PURR OS system settings app (.claw)
// Uses purr_win.h — compatible with KittenUI (LVGL) and MiniWin.
//
// Sections: Theme  |  Display  |  Storage  |  Input
// Settings are persisted to NVS under namespace "purr_settings".

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"

#define NVS_NS  "purr_settings"

// ── State ─────────────────────────────────────────────────────────────────────

static purr_win_t  s_win       = 0;
static purr_wid_t  s_status    = 0;   // bottom status label
static purr_wid_t  s_brightness_lbl = 0;

static uint8_t     s_brightness = 255;
static char        s_theme[16]  = "wce";

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
    nvs_close(h);
}

static void set_status(const char *msg) {
    purr_win_label_set(s_status, msg);
}

// ── Theme buttons ─────────────────────────────────────────────────────────────

static void apply_theme_nvs(const char *id) {
    // Persist to both kittenui namespace (picked up by kittenui_module.c)
    // and purr_settings namespace (for our own display).
    nvs_handle_t h;
    if (nvs_open("kittenui", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "theme", id);
        nvs_commit(h);
        nvs_close(h);
    }
    nvs_save_str("theme", id);
    strncpy(s_theme, id, sizeof(s_theme) - 1);

    char msg[48];
    snprintf(msg, sizeof(msg), "Theme set to '%s' — reboot to apply.", id);
    set_status(msg);
}

static void on_theme_wce(purr_wid_t w, purr_event_t e, void *u)  { (void)w;(void)e;(void)u; apply_theme_nvs("wce");  }
static void on_theme_luna(purr_wid_t w, purr_event_t e, void *u) { (void)w;(void)e;(void)u; apply_theme_nvs("luna"); }
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

// ── Build UI ──────────────────────────────────────────────────────────────────

static int settings_init(void) {
    nvs_load();

    s_win = purr_win_create("Settings");

    // ── Theme section ──────────────────────────────────────────────────────
    purr_win_label(s_win, "Theme");
    purr_wid_t tr = purr_win_row(s_win, 4);
    purr_win_button(s_win, "WCE Classic", on_theme_wce,  NULL);
    purr_win_button(s_win, "Luna",        on_theme_luna, NULL);
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

    // ── Storage section ────────────────────────────────────────────────────
    purr_win_label(s_win, "Storage");
    purr_wid_t sr = purr_win_row(s_win, 4);
    purr_win_button(s_win, "SD Status", on_sd_refresh, NULL);
    purr_win_layout_end(sr);

    // ── System section ─────────────────────────────────────────────────────
    purr_win_label(s_win, "System");
    purr_wid_t sys = purr_win_row(s_win, 4);
    purr_win_button(s_win, "Reboot", on_reboot, NULL);
    purr_win_layout_end(sys);

    // ── Status bar ─────────────────────────────────────────────────────────
    s_status = purr_win_label(s_win, "Ready.");

    purr_win_show(s_win);
    return 0;
}

static void settings_deinit(void) {
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
    .kernel_min        = "0.9.0",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = settings_init,
    .deinit            = settings_deinit,
};
