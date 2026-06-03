// purr_bootloader.cpp — PURR OS recovery / OTA bootloader shell.
// OTA 0 = PURR OS slot (re-install / remove)
// OTA 1 = Custom firmware slot (launch / install / delete)
// Entered on boot via GPIO 0 hold, NVS boot_mode flag, or bootloader_only flag.

#ifdef PURR_HAS_BOOTLOADER

#include "purr_bootloader.h"
#include "display_ili9341.h"
#include "touch_cst816s.h"
#include "partition_manager.h"
#include "../kitt.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_app_desc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

extern KITT kitt;

// ── Colour palette ────────────────────────────────────────────────────────────
static const uint16_t COL_BG     = 0x0883;
static const uint16_t COL_CARD   = 0x10A3;
static const uint16_t COL_ACCENT = 0x3CDF;
static const uint16_t COL_TEXT   = 0xFFFF;
static const uint16_t COL_SUB    = 0x8C51;
static const uint16_t COL_OK     = 0x2E48;
static const uint16_t COL_DANGER = 0xF186;
static const uint16_t COL_WARN   = 0xF400;
static const uint16_t COL_DIV    = 0x1906;
static const uint16_t COL_BTN    = 0x2965;

// ── Layout ────────────────────────────────────────────────────────────────────
#define SCR_W    320
#define SCR_H    240
#define HDR_H    26
#define FTR_H    27
#define SLOT_Y0  30
#define SLOT_H   88
#define SLOT_Y1  (SLOT_Y0 + SLOT_H + 4)
#define BTN_X    218
#define BTN_W    94
#define BTN_H    28

// ── Button tags ───────────────────────────────────────────────────────────────
#define TAG_REINSTALL   0x10
#define TAG_REMOVE      0x11
#define TAG_LAUNCH_CUST 0x20
#define TAG_INSTALL_CUST 0x21
#define TAG_DELETE_CUST  0x22
#define TAG_YES          0xE0
#define TAG_NO           0xE1
#define TAG_FILE(i)      (0xF0 | (i))

// ── Screen state ──────────────────────────────────────────────────────────────
enum PbScreen {
    PB_HOME,
    PB_CONFIRM_REMOVE,
    PB_CONFIRM_LAUNCH,
    PB_CONFIRM_DELETE,
    PB_INSTALL_SELECT,   // s_install_purr=true → PURR slot, false → custom slot
    PB_INSTALLING
};

static PbScreen         s_screen      = PB_HOME;
static bool             s_install_purr = false;
static pm_sd_file_t     s_files[PM_SD_MAX_FILES];
static int              s_nfiles      = 0;
static volatile int     s_install_pct = 0;
static char             s_install_msg[64] = "";
static volatile bool    s_dirty       = true;
static bool             s_prev_pressed = false;
static uint32_t         s_debounce_ms  = 0;

static struct { int slot; char path[PM_PATH_LEN]; char name[PM_NAME_LEN]; } s_inst;

// ── Draw helpers ──────────────────────────────────────────────────────────────
static void ps_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    display_ili9341_fill_rect(x, y, w, h, c);
}
static void ps_hline(int16_t x, int16_t y, int16_t w, uint16_t c) {
    display_ili9341_draw_hline(x, y, w, c);
}
static void ps_str(int16_t x, int16_t y, const char* s, uint16_t fg, uint16_t bg, uint8_t sz) {
    display_ili9341_draw_string(x, y, s, fg, bg, sz);
}
static void ps_strf(int16_t x, int16_t y, uint16_t fg, uint16_t bg, uint8_t sz,
                    const char* fmt, ...) {
    char buf[80]; va_list va; va_start(va, fmt); vsnprintf(buf, sizeof(buf), fmt, va); va_end(va);
    ps_str(x, y, buf, fg, bg, sz);
}

// ── Hit table ─────────────────────────────────────────────────────────────────
struct Btn { int16_t x, y, w, h; int tag; };
static Btn  s_btns[16];
static int  s_nbtn = 0;

static void ps_btn(int16_t x, int16_t y, int16_t w, int16_t h,
                   const char* label, uint16_t fg, uint16_t bg, int tag) {
    ps_fill(x, y, w, h, bg);
    int16_t tx = x + (w - (int16_t)(strlen(label) * 6)) / 2;
    int16_t ty = y + (h - 8) / 2;
    ps_str(tx, ty, label, fg, bg, 1);
    if (s_nbtn < 16) s_btns[s_nbtn++] = {x, y, w, h, tag};
}

static int hit_test(int16_t tx, int16_t ty) {
    for (int i = 0; i < s_nbtn; i++) {
        const Btn& b = s_btns[i];
        if (tx >= b.x && tx < b.x+b.w && ty >= b.y && ty < b.y+b.h)
            return b.tag;
    }
    return -1;
}

// ── Header ────────────────────────────────────────────────────────────────────
static void render_header() {
    ps_fill(0, 0, SCR_W, HDR_H, COL_BG);
    ps_str(8, 5, "PURR OS Bootloader", COL_ACCENT, COL_BG, 2);
    KITT::memory_stats_t mem; kitt.memory_get_stats(&mem);
    char right[40];
    snprintf(right, sizeof(right), "%s  %luK", kitt.device_name(),
             (unsigned long)mem.free_ram_kb);
    ps_str(SCR_W - (int16_t)(strlen(right)*6) - 6, 9, right, COL_SUB, COL_BG, 1);
    ps_hline(0, HDR_H, SCR_W, COL_DIV);
}

// Refresh just the RAM counter in header (no full redraw — prevents flicker)
static void refresh_header_ram() {
    KITT::memory_stats_t mem; kitt.memory_get_stats(&mem);
    char right[40];
    snprintf(right, sizeof(right), "%s  %luK", kitt.device_name(),
             (unsigned long)mem.free_ram_kb);
    // Blank old area then redraw
    ps_fill(130, 5, 186, 14, COL_BG);
    ps_str(SCR_W - (int16_t)(strlen(right)*6) - 6, 9, right, COL_SUB, COL_BG, 1);
}

// ── Footer ────────────────────────────────────────────────────────────────────
static void render_footer() {
    ps_hline(0, SCR_H - FTR_H - 1, SCR_W, COL_DIV);
    ps_fill(0, SCR_H - FTR_H, SCR_W, FTR_H, COL_BG);
    ps_str(8, SCR_H - FTR_H + 9,
           pm_sd_available() ? "SD: ready" : "SD: none",
           pm_sd_available() ? COL_OK : COL_WARN, COL_BG, 1);
}

// ── PURR OS card (OTA 0 slot) ─────────────────────────────────────────────────
static void render_purr_card(int16_t y) {
    ps_fill(4, y, SCR_W - 8, SLOT_H, COL_CARD);

    // Title
    ps_str(12, y + 8,  "OTA 0", COL_SUB, COL_CARD, 1);
    ps_str(12, y + 20, "PURR OS", COL_TEXT, COL_CARD, 2);

    // Version from running app description
    const esp_app_desc_t* desc = esp_app_get_description();
    ps_strf(12, y + 42, COL_SUB, COL_CARD, 1, "v%s", desc ? desc->version : "?.?.?");

    // Port info
    ps_str(12, y + 54, "USB \xB7 UART0", COL_ACCENT, COL_CARD, 1);

    ps_btn(BTN_X, y + 10, BTN_W, BTN_H, "REINSTALL", COL_BG, COL_ACCENT, TAG_REINSTALL);
    ps_btn(BTN_X, y + 48, BTN_W, BTN_H, "REMOVE",    COL_BG, COL_DANGER,  TAG_REMOVE);
}

// ── Custom firmware card (OTA 1 slot) ────────────────────────────────────────
static void render_custom_card(int16_t y) {
    pm_slot_t sl;
    bool ok = pm_slot_info(1, &sl);  // OTA 1 = custom firmware

    ps_fill(4, y, SCR_W - 8, SLOT_H, COL_CARD);
    ps_str(12, y + 8, "OTA 1", COL_SUB, COL_CARD, 1);

    if (!ok) {
        ps_str(12, y + 36, "ERR: slot unavailable", COL_DANGER, COL_CARD, 1);
        return;
    }

    if (sl.valid) {
        ps_str(12, y + 20, sl.name[0] ? sl.name : "(unnamed)", COL_TEXT, COL_CARD, 2);
        ps_strf(12, y + 42, COL_SUB, COL_CARD, 1, "%.2f MB  [%s]",
                sl.size_bytes / 1048576.0f, sl.label);
        ps_btn(BTN_X, y + 10, BTN_W, BTN_H, "LAUNCH",  COL_BG, COL_OK,     TAG_LAUNCH_CUST);
        ps_btn(BTN_X, y + 48, BTN_W, BTN_H, "DELETE",  COL_BG, COL_DANGER,  TAG_DELETE_CUST);
    } else {
        ps_str(12, y + 22, "EMPTY", COL_SUB, COL_CARD, 2);
        ps_str(12, y + 44, "No custom firmware installed", COL_SUB, COL_CARD, 1);
        ps_btn(BTN_X, y + 30, BTN_W, BTN_H, "INSTALL", COL_TEXT, COL_ACCENT, TAG_INSTALL_CUST);
    }
}

// ── Home screen ───────────────────────────────────────────────────────────────
static void render_home() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);
    render_header();
    render_purr_card(SLOT_Y0);
    render_custom_card(SLOT_Y1);
    render_footer();
}

// ── Confirm overlay ───────────────────────────────────────────────────────────
static void render_confirm_remove() {
    s_nbtn = 0;
    // Darken with overlay
    ps_fill(30, 40, 260, 155, COL_CARD);
    ps_hline(30, 40,  260, COL_DANGER);
    ps_hline(30, 194, 260, COL_DANGER);

    ps_str(42, 52,  "Remove PURR OS?", COL_TEXT,   COL_CARD, 2);
    ps_str(42, 76,  "This will wipe the OS and lock", COL_WARN, COL_CARD, 1);
    ps_str(42, 88,  "the device into bootloader-only", COL_WARN, COL_CARD, 1);
    ps_str(42, 100, "mode until reinstalled.", COL_WARN, COL_CARD, 1);
    ps_str(42, 116, "Are you sure?", COL_DANGER, COL_CARD, 1);

    ps_btn(40,  160, 100, 28, "CANCEL", COL_TEXT, COL_BTN,   TAG_NO);
    ps_btn(180, 160, 100, 28, "REMOVE", COL_BG,   COL_DANGER, TAG_YES);
}

static void render_confirm_launch() {
    s_nbtn = 0;
    pm_slot_t sl; pm_slot_info(1, &sl);
    ps_fill(50, 60, 220, 120, COL_CARD);
    ps_hline(50, 60,  220, COL_OK);
    ps_hline(50, 179, 220, COL_OK);
    ps_str(62, 72, "Launch Custom Firmware?", COL_TEXT, COL_CARD, 2);
    ps_str(62, 96, sl.name[0] ? sl.name : "(unnamed)", COL_SUB, COL_CARD, 1);
    ps_str(62, 110, "PURR OS will not run until reboot.", COL_WARN, COL_CARD, 1);
    ps_btn(60,  142, 90, 28, "CANCEL", COL_TEXT, COL_BTN, TAG_NO);
    ps_btn(180, 142, 80, 28, "LAUNCH", COL_BG,   COL_OK,  TAG_YES);
}

static void render_confirm_delete() {
    s_nbtn = 0;
    pm_slot_t sl; pm_slot_info(1, &sl);
    ps_fill(50, 60, 220, 120, COL_CARD);
    ps_hline(50, 60,  220, COL_DANGER);
    ps_hline(50, 179, 220, COL_DANGER);
    ps_str(62, 72, "Delete Custom Firmware?", COL_TEXT, COL_CARD, 2);
    ps_str(62, 96, sl.name[0] ? sl.name : "(unnamed)", COL_SUB, COL_CARD, 1);
    ps_str(62, 110, "This cannot be undone.", COL_WARN, COL_CARD, 1);
    ps_btn(60,  142, 90, 28, "CANCEL", COL_TEXT, COL_BTN,    TAG_NO);
    ps_btn(180, 142, 80, 28, "DELETE", COL_BG,   COL_DANGER, TAG_YES);
}

// ── SD file picker ────────────────────────────────────────────────────────────
static void render_install_select() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);
    render_header();

    const char* title = s_install_purr
        ? "Reinstall PURR OS — select .bin:"
        : "Install custom firmware — select .bin:";
    ps_str(8, HDR_H + 6, title, COL_SUB, COL_BG, 1);
    ps_hline(0, HDR_H + 16, SCR_W, COL_DIV);

    if (!pm_sd_available()) {
        ps_str(8, HDR_H + 30, "No SD card detected.", COL_WARN, COL_BG, 1);
    } else if (s_nfiles == 0) {
        ps_str(8, HDR_H + 30, "No .bin files on SD root.", COL_WARN, COL_BG, 1);
    } else {
        for (int i = 0; i < s_nfiles && i < 4; i++) {
            int16_t fy = HDR_H + 22 + i * 38;
            ps_fill(4, fy, SCR_W - 8, 34, COL_CARD);
            ps_str(10, fy + 6,  s_files[i].name, COL_TEXT, COL_CARD, 1);
            ps_strf(10, fy + 18, COL_SUB, COL_CARD, 1, "%.2f MB", s_files[i].size_bytes / 1048576.0f);
            ps_btn(SCR_W - 90, fy + 4, 80, 26, "SELECT", COL_BG, COL_ACCENT, TAG_FILE(i));
        }
    }

    ps_hline(0, SCR_H - FTR_H - 1, SCR_W, COL_DIV);
    ps_btn(8, SCR_H - FTR_H + 4, 60, 20, "BACK", COL_TEXT, COL_BTN, TAG_NO);
}

// ── Install progress ──────────────────────────────────────────────────────────
static void render_installing() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);
    render_header();
    ps_strf(8, HDR_H + 22, COL_TEXT, COL_BG, 2, "Installing...");
    ps_fill(8, HDR_H + 52, SCR_W - 16, 22, COL_DIV);
    int bar = (int)((SCR_W - 16) * s_install_pct / 100);
    if (bar > 0) ps_fill(8, HDR_H + 52, bar, 22, COL_ACCENT);
    char pct[8]; snprintf(pct, sizeof(pct), "%d%%", s_install_pct);
    ps_str(SCR_W / 2 - 12, HDR_H + 56, pct, COL_TEXT,
           s_install_pct > 30 ? COL_ACCENT : COL_DIV, 1);
    ps_str(8, HDR_H + 84, s_install_msg, COL_SUB, COL_BG, 1);
}

// ── Install background task ───────────────────────────────────────────────────
static void install_task_fn(void*) {
    pm_install(s_inst.slot, s_inst.path, s_inst.name,
               [](int pct, const char* status) {
                   s_install_pct = pct;
                   strlcpy(s_install_msg, status, sizeof(s_install_msg));
                   s_dirty = true;
               });
    s_screen = PB_HOME;
    s_dirty  = true;
    vTaskDelete(nullptr);
}

// ── Action handler ────────────────────────────────────────────────────────────
static void on_tap(int tag) {
    switch (s_screen) {

    case PB_HOME:
        if (tag == TAG_REINSTALL) {
            s_install_purr = true;
            s_nfiles = pm_sd_available() ? pm_sd_list(s_files, PM_SD_MAX_FILES) : 0;
            s_screen = PB_INSTALL_SELECT;
        } else if (tag == TAG_REMOVE) {
            s_screen = PB_CONFIRM_REMOVE;
        } else if (tag == TAG_LAUNCH_CUST) {
            s_screen = PB_CONFIRM_LAUNCH;
        } else if (tag == TAG_INSTALL_CUST) {
            s_install_purr = false;
            s_nfiles = pm_sd_available() ? pm_sd_list(s_files, PM_SD_MAX_FILES) : 0;
            s_screen = PB_INSTALL_SELECT;
        } else if (tag == TAG_DELETE_CUST) {
            s_screen = PB_CONFIRM_DELETE;
        }
        break;

    case PB_CONFIRM_REMOVE:
        if (tag == TAG_YES) {
            // Set bootloader-only flag — explorer will never launch again until reinstalled
            Preferences p; p.begin("purr_boot", false);
            p.putBool("bootloader_only", true); p.end();
            pm_delete(0);   // wipe OTA 0 (PURR OS slot)
            delay(200);
            esp_restart();
        } else if (tag == TAG_NO) {
            s_screen = PB_HOME;
        }
        break;

    case PB_CONFIRM_LAUNCH:
        if (tag == TAG_YES) { pm_launch(1); }
        else if (tag == TAG_NO) { s_screen = PB_HOME; }
        break;

    case PB_CONFIRM_DELETE:
        if (tag == TAG_YES) { pm_delete(1); s_screen = PB_HOME; }
        else if (tag == TAG_NO) { s_screen = PB_HOME; }
        break;

    case PB_INSTALL_SELECT:
        if (tag == TAG_NO) {   // BACK button
            s_screen = PB_HOME;
        } else if ((tag & 0xF0) == 0xF0) {
            int fi = tag & 0x0F;
            if (fi < s_nfiles) {
                s_inst.slot = s_install_purr ? 0 : 1;
                strlcpy(s_inst.path, s_files[fi].path, PM_PATH_LEN);
                strlcpy(s_inst.name, s_files[fi].name, PM_NAME_LEN);
                s_install_pct = 0;
                strlcpy(s_install_msg, "Preparing...", sizeof(s_install_msg));
                s_screen = PB_INSTALLING;
                s_dirty  = true;
                xTaskCreate(install_task_fn, "pm_inst", 8192, nullptr, 4, nullptr);
                return;
            }
        }
        break;

    default: break;
    }
    s_dirty = true;
}

// ── Touch ─────────────────────────────────────────────────────────────────────
static void handle_touch() {
    cst_touch_event_t ev;
    bool pressed = touch_cst816s_get_event(&ev) && ev.pressed;
    if (pressed && !s_prev_pressed) {
        uint32_t now = millis();
        if (now - s_debounce_ms >= 300) {
            s_debounce_ms = now;
            int tag = hit_test(ev.x, ev.y);
            if (tag >= 0) on_tap(tag);
        }
    }
    s_prev_pressed = pressed;
}

// ── LED (blue in bootloader mode) ────────────────────────────────────────────
#define CYD_LED_R  4
#define CYD_LED_G 16
#define CYD_LED_B 17

static void led_init() {
    pinMode(CYD_LED_R, OUTPUT); digitalWrite(CYD_LED_R, HIGH);
    pinMode(CYD_LED_G, OUTPUT); digitalWrite(CYD_LED_G, HIGH);
    pinMode(CYD_LED_B, OUTPUT); digitalWrite(CYD_LED_B, HIGH);
}
static void led_heartbeat() {
    static uint32_t ms = 0; static bool on = false;
    if (millis() - ms >= 500) { ms = millis(); on = !on;
        digitalWrite(CYD_LED_B, on ? LOW : HIGH); }
}

// ── Main task ─────────────────────────────────────────────────────────────────
static void purr_bootloader_task(void*) {
    led_init();
    pm_init();
    s_dirty = true;

    static uint32_t s_last_tick = 0;

    while (true) {
        handle_touch();

        if (s_dirty) {
            s_dirty = false;
            switch (s_screen) {
            case PB_HOME:             render_home(); break;
            case PB_CONFIRM_REMOVE:   render_home(); render_confirm_remove(); break;
            case PB_CONFIRM_LAUNCH:   render_home(); render_confirm_launch(); break;
            case PB_CONFIRM_DELETE:   render_home(); render_confirm_delete(); break;
            case PB_INSTALL_SELECT:   render_install_select(); break;
            case PB_INSTALLING:       render_installing(); break;
            }
        }

        // Tick: only refresh RAM counter in header (no full redraw = no flicker)
        // and update progress screen if installing
        uint32_t now = millis();
        if (now - s_last_tick >= 2000) {
            s_last_tick = now;
            if (s_screen == PB_HOME) refresh_header_ram();
            else if (s_screen == PB_INSTALLING) s_dirty = true;
        }

        led_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void purr_bootloader_start() {
    xTaskCreatePinnedToCore(purr_bootloader_task, "purr_boot", 8192, nullptr, 3, nullptr, 1);
}

void purr_bootloader_request_reboot() {
    Preferences prefs;
    prefs.begin("purr_boot", false);
    prefs.putString("boot_mode", "bootloader");
    prefs.end();
    delay(100);
    esp_restart();
}

#endif  // PURR_HAS_BOOTLOADER
