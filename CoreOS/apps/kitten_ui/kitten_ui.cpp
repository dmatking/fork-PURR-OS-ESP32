// kitten_ui.cpp — KittenUI text-mode shell for small displays (SSD1306 128×64)
// Replaces the old "smol" shell. Pure ESP-IDF, no Arduino dependency.
// Targets: Heltec WiFi LoRa 32 V3, LilyGo T-Deck / T-Deck Plus.

#include "kitten_ui.h"
#include "../../system/kernel/kitt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "kitten_ui";

extern KITT kitt;

// ── Layout ────────────────────────────────────────────────────────────────────
// 128×64 OLED: 8 rows × ~21 chars at font 6×8

static constexpr int LIST_ROWS         = 4;   // rows 1–4 (row 0 = status bar)
static constexpr int MAX_APPS          = 32;
static constexpr int REFRESH_ACTIVE_MS = 150;
static constexpr int REFRESH_IDLE_MS   = 3000;
static constexpr int IDLE_AFTER_MS     = 3000;

// ── Screens ───────────────────────────────────────────────────────────────────

enum Screen {
    SCR_DESKTOP,
    SCR_MENU,
    SCR_ABOUT,
    SCR_SYSINFO,
    SCR_WIFI,
    SCR_LORA,
};

static const char *MENU_ITEMS[] = {
    "About",
    "System Info",
    "WiFi Status",
    "LoRa Status",
    "Reboot",
};
static constexpr int MENU_COUNT = 5;

// ── State ─────────────────────────────────────────────────────────────────────

static Screen  s_screen     = SCR_DESKTOP;
static int     s_cursor     = 0;
static int     s_scroll_off = 0;
static bool    s_confirm    = false;
static int     s_menu_sel   = 0;

static KITT::app_entry_t s_apps[MAX_APPS];
static int               s_app_count = 0;

static bool s_child_running = false;
static char s_child_path[128] = {};

static uint32_t s_last_input_ms   = 0;
static uint32_t s_select_press_ms = 0;  // when SELECT was pressed down

#define HOLD_THRESHOLD_MS  500  // >= 500ms held = "hold" (launch/confirm)
                                // <  500ms        = "click" (move cursor)

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint32_t now_ms() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void rescan() {
    kitt.apps_scan();
    s_app_count = 0;
    int total = kitt.app_list_count();
    for (int i = 0; i < total && s_app_count < MAX_APPS; i++) {
        KITT::app_entry_t e;
        kitt.app_get_entry(i, &e);
        if (strncasecmp(e.name, "kitten_ui", 9) == 0) continue;
        if (strncasecmp(e.name, "smol",       4) == 0) continue;
        if (strncasecmp(e.name, "explorer",   8) == 0) continue;
        if (strncasecmp(e.name, "classicmac",10) == 0) continue;
        s_apps[s_app_count++] = e;
    }
    if (s_cursor >= s_app_count)
        s_cursor = (s_app_count > 0) ? s_app_count - 1 : 0;
}

// ── Status bar (row 0) ────────────────────────────────────────────────────────
// Format: "W L  ##K" — W=wifi, L=lora, ##K=free ram

static void draw_status_bar() {
    char bar[22] = {};
    char wifi = kitt.wifi_enabled()
                    ? (kitt.wifi_connected() ? 'W' : 'w')
                    : '-';
    char lora = kitt.lora_enabled() ? 'L' : '-';

    KITT::memory_stats_t mem;
    kitt.memory_get_stats(&mem);

    // "W L  128K" right-padded to 16 chars
    snprintf(bar, sizeof(bar), "%c %c  %3luK",
             wifi, lora, (unsigned long)mem.free_ram_kb);
    kitt.text_print(0, bar);
}

// ── Draw routines ─────────────────────────────────────────────────────────────

static void draw_desktop() {
    if (s_cursor < s_scroll_off)
        s_scroll_off = s_cursor;
    if (s_cursor >= s_scroll_off + LIST_ROWS)
        s_scroll_off = s_cursor - LIST_ROWS + 1;

    draw_status_bar();

    // Separator
    kitt.text_print(1, "----------------");

    for (int i = 0; i < LIST_ROWS; i++) {
        int  idx  = s_scroll_off + i;
        char line[22] = {};
        if (idx < s_app_count) {
            bool sel = (idx == s_cursor);
            snprintf(line, sizeof(line), "%c%.13s%s",
                     sel ? '>' : ' ',
                     s_apps[idx].name,
                     sel ? (s_confirm ? " OK" : " ?") : "");
        }
        kitt.text_print(2 + i, line);
    }

    // Hint row
    if (s_app_count == 0) {
        kitt.text_print(6, "No apps found");
        kitt.text_print(7, "BCK:menu");
    } else {
        kitt.text_print(6, s_confirm ? "HOLD:launch CLK:next" : "CLK:move  BCK:menu");
        kitt.text_print(7, "DN/UP:scroll");
    }
}

static void draw_menu() {
    kitt.text_clear();
    kitt.text_print(0, "--- PURR MENU ---");
    for (int i = 0; i < MENU_COUNT; i++) {
        char line[22];
        snprintf(line, sizeof(line), "%c %.12s",
                 (i == s_menu_sel) ? '>' : ' ', MENU_ITEMS[i]);
        kitt.text_print(1 + i, line);
    }
    kitt.text_print(7, "CLK:next HOLD:open BCK:back");
}

static void draw_about() {
    KITT::memory_stats_t mem;
    kitt.memory_get_stats(&mem);

    char line_ver[22], line_dev[22], line_res[22], line_mem[22];
    snprintf(line_ver, sizeof(line_ver), "PURR OS " PURR_OS_VERSION);
    snprintf(line_dev, sizeof(line_dev), "%.20s", kitt.device_name());
    snprintf(line_res, sizeof(line_res), "Disp %ux%u",
             kitt.display_width(), kitt.display_height());
    snprintf(line_mem, sizeof(line_mem), "RAM %lu/%luK free",
             (unsigned long)mem.free_ram_kb,
             (unsigned long)mem.total_ram_kb);

    kitt.text_clear();
    kitt.text_print(0, "--- About ---");
    kitt.text_print(1, line_ver);
    kitt.text_print(2, "KITT " KITT_VERSION);
    kitt.text_print(3, line_dev);
    kitt.text_print(4, line_res);
    kitt.text_print(5, line_mem);
    kitt.text_print(6, "");
    kitt.text_print(7, "any key: back");
}

static void draw_sysinfo() {
    KITT::memory_stats_t mem;
    kitt.memory_get_stats(&mem);

    uint32_t up_s = now_ms() / 1000;
    uint32_t h    = up_s / 3600;
    uint32_t m    = (up_s % 3600) / 60;
    uint32_t s    = up_s % 60;

    char line_ram[22], line_cpu[22], line_up[22], line_psram[22];
    snprintf(line_ram,   sizeof(line_ram),   "RAM  %lu/%luK",
             (unsigned long)mem.free_ram_kb,
             (unsigned long)mem.total_ram_kb);
    snprintf(line_cpu,   sizeof(line_cpu),   "CPU  %dMHz",
             kitt.cpu_get_freq_mhz());
    snprintf(line_up,    sizeof(line_up),    "Up   %lu:%02lu:%02lu", h, m, s);
    snprintf(line_psram, sizeof(line_psram), "PSRAM%lu/%luK",
             (unsigned long)mem.psram_free_kb,
             (unsigned long)mem.psram_total_kb);

    kitt.text_clear();
    kitt.text_print(0, "--- Sys Info ---");
    kitt.text_print(1, line_ram);
    kitt.text_print(2, line_cpu);
    kitt.text_print(3, line_up);
    kitt.text_print(4, mem.psram_total_kb > 0 ? line_psram : "PSRAM none");
    kitt.text_print(5, "");
    kitt.text_print(6, "");
    kitt.text_print(7, "any key: back");
}

static void draw_wifi() {
    char ssid[22]  = {};
    char line[22]  = {};
    bool en        = kitt.wifi_enabled();
    bool conn      = en && kitt.wifi_connected();

    kitt.text_clear();
    kitt.text_print(0, "--- WiFi ---");
    kitt.text_print(1, en   ? "Status: on"        : "Status: off");
    kitt.text_print(2, conn ? "Link:   connected"  : "Link:   none");

    if (conn) {
        kitt.wifi_get_connected_ssid(ssid, sizeof(ssid));
        snprintf(line, sizeof(line), "SSID: %.14s", ssid);
        kitt.text_print(3, line);
        snprintf(line, sizeof(line), "RSSI: %ddBm",
                 kitt.wifi_signal_strength());
        kitt.text_print(4, line);
    } else {
        kitt.text_print(3, "");
        kitt.text_print(4, "");
    }

    kitt.text_print(5, "");
    kitt.text_print(6, "");
    kitt.text_print(7, "any key: back");
}

static void draw_lora() {
    char line[22] = {};
    bool en = kitt.lora_enabled();

    kitt.text_clear();
    kitt.text_print(0, "--- LoRa ---");
    kitt.text_print(1, en ? "Status: on" : "Status: off / N/A");

    if (en) {
        snprintf(line, sizeof(line), "Freq: %luMHz",
                 (unsigned long)(kitt.lora_get_frequency() / 1000000));
        kitt.text_print(2, line);
        snprintf(line, sizeof(line), "Pwr:  %ddBm",
                 (int)kitt.lora_get_power());
        kitt.text_print(3, line);
        snprintf(line, sizeof(line), "RSSI: %ddBm",
                 kitt.lora_get_rssi());
        kitt.text_print(4, line);
        kitt.text_print(5, kitt.lora_busy() ? "TX: in progress" : "TX: idle");
    } else {
        kitt.text_print(2, "Enable in build:");
        kitt.text_print(3, "PURR_ENABLE_LORA");
        kitt.text_print(4, "");
        kitt.text_print(5, "");
    }

    kitt.text_print(6, "");
    kitt.text_print(7, "any key: back");
}

// ── Input handlers ────────────────────────────────────────────────────────────

// held=true  → long press (>= HOLD_THRESHOLD_MS): launch / confirm action
// held=false → short click: move cursor / navigate
static void handle_desktop(KITT::generic_key_t key, bool held) {
    int n = s_app_count;
    switch (key) {
    case KITT::KEY_SELECT:
        if (held) {
            // Hold = launch selected app
            if (n > 0) {
                s_confirm = false;
                strncpy(s_child_path, s_apps[s_cursor].path, sizeof(s_child_path) - 1);
                if (kitt.app_launch(s_apps[s_cursor].path)) {
                    s_child_running = true;
                } else {
                    kitt.text_clear();
                    kitt.text_print(0, "Launch failed");
                    kitt.text_print(1, "Check serial log");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
            }
        } else {
            // Click = move cursor to next app
            if (n > 0) s_cursor = (s_cursor + 1) % n;
            s_confirm = true;  // cursor is now on an app, hint shows "HOLD:launch"
        }
        break;
    case KITT::KEY_DOWN:
        if (n > 0) s_cursor = (s_cursor + 1) % n;
        s_confirm = true;
        break;
    case KITT::KEY_UP:
        if (n > 0) s_cursor = (s_cursor - 1 + n) % n;
        s_confirm = true;
        break;
    case KITT::KEY_BACK:
        s_menu_sel = 0;
        s_screen   = SCR_MENU;
        break;
    default:
        break;
    }
}

static void handle_menu(KITT::generic_key_t key, bool held) {
    switch (key) {
    case KITT::KEY_DOWN:
        s_menu_sel = (s_menu_sel + 1) % MENU_COUNT;
        break;
    case KITT::KEY_UP:
        s_menu_sel = (s_menu_sel - 1 + MENU_COUNT) % MENU_COUNT;
        break;
    case KITT::KEY_SELECT:
        if (!held) {
            // Click just moves selection down (cycle through items)
            s_menu_sel = (s_menu_sel + 1) % MENU_COUNT;
        } else {
            // Hold = confirm menu item
            switch (s_menu_sel) {
            case 0: s_screen = SCR_ABOUT;   break;
            case 1: s_screen = SCR_SYSINFO; break;
            case 2: s_screen = SCR_WIFI;    break;
            case 3: s_screen = SCR_LORA;    break;
            case 4: esp_restart();          break;
            default: s_screen = SCR_DESKTOP; break;
            }
        }
        break;
    case KITT::KEY_BACK:
        s_screen = SCR_DESKTOP;
        break;
    default:
        break;
    }
}

// ── Task ──────────────────────────────────────────────────────────────────────

static void kitten_ui_task(void*) {
    ESP_LOGI(TAG, "KittenUI starting");
    rescan();
    s_last_input_ms = now_ms();

    while (true) {
        // Detect child exit
        if (s_child_running && !kitt.process_running(s_child_path)) {
            s_child_running = false;
            s_child_path[0] = '\0';
            rescan();
        }

        // Drain key queue — use raw events to measure SELECT hold duration
        KITT::generic_key_t key;
        bool pressed;
        while (kitt.get_key_event(&key, &pressed)) {
            if (s_child_running) continue;

            s_last_input_ms = now_ms();

            if (key == KITT::KEY_SELECT) {
                if (pressed) {
                    // Record when the button went down
                    s_select_press_ms = now_ms();
                } else {
                    // On release, classify as click or hold
                    bool held = (now_ms() - s_select_press_ms) >= HOLD_THRESHOLD_MS;

                    if (s_screen == SCR_ABOUT   ||
                        s_screen == SCR_SYSINFO ||
                        s_screen == SCR_WIFI    ||
                        s_screen == SCR_LORA) {
                        s_screen = SCR_DESKTOP;
                    } else if (s_screen == SCR_MENU) {
                        handle_menu(key, held);
                    } else {
                        handle_desktop(key, held);
                    }
                }
            } else {
                if (!pressed) continue;  // only act on press for other keys

                if (s_screen == SCR_ABOUT   ||
                    s_screen == SCR_SYSINFO ||
                    s_screen == SCR_WIFI    ||
                    s_screen == SCR_LORA) {
                    s_screen = SCR_DESKTOP;
                } else if (s_screen == SCR_MENU) {
                    handle_menu(key, false);
                } else {
                    handle_desktop(key, false);
                }
            }
        }

        // Redraw
        if (!s_child_running) {
            switch (s_screen) {
            case SCR_DESKTOP: draw_desktop(); break;
            case SCR_MENU:    draw_menu();    break;
            case SCR_ABOUT:   draw_about();   break;
            case SCR_SYSINFO: draw_sysinfo(); break;
            case SCR_WIFI:    draw_wifi();    break;
            case SCR_LORA:    draw_lora();    break;
            }
        }

        uint32_t age      = now_ms() - s_last_input_ms;
        uint32_t delay_ms = (age < (uint32_t)IDLE_AFTER_MS)
                            ? REFRESH_ACTIVE_MS
                            : REFRESH_IDLE_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void kitten_ui_start() {
    xTaskCreatePinnedToCore(kitten_ui_task, "kitten_ui", 4096, nullptr, 2, nullptr, 1);
}
