#include "smol.h"
#include "../../system/kernel/kitt.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern KITT kitt;

// ── Layout constants ──────────────────────────────────────────────────────────

static constexpr int LIST_ROWS    = 5;   // rows 2–6
static constexpr int MAX_APPS     = 32;
static constexpr int REFRESH_ACTIVE_MS = 150;
static constexpr int REFRESH_IDLE_MS   = 3000;
static constexpr int IDLE_AFTER_MS     = 3000;

// ── State ─────────────────────────────────────────────────────────────────────

enum SmolScreen { SCR_DESKTOP, SCR_MENU, SCR_ABOUT, SCR_SYSINFO };

static const char* MENU_ITEMS[] = { "About", "System Info", "Quit PURR OS" };
static constexpr int MENU_COUNT = 3;

static SmolScreen screen      = SCR_DESKTOP;
static int  cursor            = 0;
static int  scroll_off        = 0;
static bool confirm           = false;
static int  menu_sel          = 0;

static KITT::app_entry_t apps[MAX_APPS];
static int  app_count         = 0;

static bool child_running     = false;
static char child_path[128]   = {};

static uint32_t last_input_ms = 0;

// ── App list ──────────────────────────────────────────────────────────────────

static void rescan() {
    kitt.apps_scan();
    app_count = 0;
    int total = kitt.app_list_count();
    for (int i = 0; i < total && app_count < MAX_APPS; i++) {
        KITT::app_entry_t e;
        kitt.app_get_entry(i, &e);
        // Skip shells and large-display-only apps
        if (strncasecmp(e.name, "smol",       4)  == 0) continue;
        if (strncasecmp(e.name, "explorer",   8)  == 0) continue;
        if (strncasecmp(e.name, "classicmac", 10) == 0) continue;
        apps[app_count++] = e;
    }
    if (cursor >= app_count) cursor = (app_count > 0) ? app_count - 1 : 0;
}

// ── Draw routines ─────────────────────────────────────────────────────────────

static void draw_desktop() {
    if (cursor < scroll_off) scroll_off = cursor;
    if (cursor >= scroll_off + LIST_ROWS) scroll_off = cursor - LIST_ROWS + 1;

    char hint[20];
    bool can_open = confirm && app_count > 0;
    snprintf(hint, sizeof(hint), "%s BCK:menu", can_open ? "SEL:open" : "SEL:next");

    kitt.text_clear();
    kitt.text_print(0, kitt.os_name());
    kitt.text_print(1, "----------------");
    for (int i = 0; i < LIST_ROWS; i++) {
        int  idx  = scroll_off + i;
        char line[17] = {};
        if (idx < app_count)
            snprintf(line, sizeof(line), "%c%.14s", (idx == cursor) ? '>' : ' ', apps[idx].name);
        kitt.text_print(2 + i, line);
    }
    kitt.text_print(7, hint);
}

static void draw_menu() {
    kitt.text_clear();
    kitt.text_print(0, "- PURR -");
    kitt.text_print(1, "");
    for (int i = 0; i < MENU_COUNT; i++) {
        char line[17];
        snprintf(line, sizeof(line), "%c%.14s", (i == menu_sel) ? '>' : ' ', MENU_ITEMS[i]);
        kitt.text_print(2 + i, line);
    }
    for (int r = 2 + MENU_COUNT; r < 7; r++) kitt.text_print(r, "");
    kitt.text_print(7, "SEL:pick BCK:back");
}

static void draw_about() {
    KITT::memory_stats_t mem;
    kitt.memory_get_stats(&mem);

    char line_dev[17], line_mem[17], line_res[17];
    snprintf(line_dev, sizeof(line_dev), "%.16s", kitt.device_name());
    snprintf(line_mem, sizeof(line_mem), "%luK free", mem.free_ram_kb);
    snprintf(line_res, sizeof(line_res), "%ux%u",
             kitt.display_width(), kitt.display_height());

    kitt.text_clear();
    kitt.text_print(0, kitt.os_name());
    kitt.text_print(1, line_dev);
    kitt.text_print(2, line_res);
    kitt.text_print(3, line_mem);
    kitt.text_print(4, "");
    kitt.text_print(5, "");
    kitt.text_print(6, "");
    kitt.text_print(7, "any key: back");
}

static void draw_sysinfo() {
    KITT::memory_stats_t mem;
    kitt.memory_get_stats(&mem);

    uint32_t up_s = millis() / 1000;
    uint32_t h    = up_s / 3600;
    uint32_t m    = (up_s % 3600) / 60;
    uint32_t s    = up_s % 60;
    int      freq = kitt.cpu_get_freq_mhz();

    char line_ram[17], line_cpu[17], line_up[17];
    snprintf(line_ram, sizeof(line_ram), "RAM %lu/%luK", mem.free_ram_kb, mem.total_ram_kb);
    snprintf(line_cpu, sizeof(line_cpu), "CPU %dMHz", freq);
    snprintf(line_up,  sizeof(line_up),  "Up %lu:%02lu:%02lu", h, m, s);

    kitt.text_clear();
    kitt.text_print(0, "System Info");
    kitt.text_print(1, line_ram);
    kitt.text_print(2, line_cpu);
    kitt.text_print(3, line_up);
    kitt.text_print(4, "");
    kitt.text_print(5, "");
    kitt.text_print(6, "");
    kitt.text_print(7, "any key: back");
}

// ── Input handlers ────────────────────────────────────────────────────────────

static void handle_desktop(KITT::generic_key_t key) {
    int n = app_count;
    switch (key) {
        case KITT::KEY_SELECT:
            if (confirm && n > 0) {
                confirm = false;
                strncpy(child_path, apps[cursor].path, sizeof(child_path) - 1);
                if (kitt.app_launch(apps[cursor].path)) {
                    child_running = true;
                } else {
                    kitt.text_clear();
                    kitt.text_print(0, "Launch failed");
                    kitt.text_print(1, "No MicroPython");
                    kitt.text_print(2, "runtime yet.");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
            } else {
                if (n > 0) cursor = (cursor + 1) % n;
                confirm = true;
            }
            break;
        case KITT::KEY_DOWN:
            if (n > 0) cursor = (cursor + 1) % n;
            confirm = true;
            break;
        case KITT::KEY_UP:
            if (n > 0) cursor = (cursor - 1 + n) % n;
            confirm = true;
            break;
        case KITT::KEY_BACK:
            menu_sel = 0;
            screen   = SCR_MENU;
            break;
        default:
            break;
    }
}

static void handle_menu(KITT::generic_key_t key) {
    switch (key) {
        case KITT::KEY_DOWN:
            menu_sel = (menu_sel + 1) % MENU_COUNT;
            break;
        case KITT::KEY_UP:
            menu_sel = (menu_sel - 1 + MENU_COUNT) % MENU_COUNT;
            break;
        case KITT::KEY_SELECT: {
            const char* item = MENU_ITEMS[menu_sel];
            if (strcmp(item, "Quit PURR OS") == 0) {
                esp_restart();
            } else if (strcmp(item, "About") == 0) {
                screen = SCR_ABOUT;
            } else if (strcmp(item, "System Info") == 0) {
                screen = SCR_SYSINFO;
            } else {
                screen = SCR_DESKTOP;
            }
            break;
        }
        case KITT::KEY_BACK:
            screen = SCR_DESKTOP;
            break;
        default:
            break;
    }
}

// ── Task ──────────────────────────────────────────────────────────────────────

static void smol_task(void*) {
    rescan();
    last_input_ms = millis();

    while (true) {
        // Detect child exit
        if (child_running && !kitt.process_running(child_path)) {
            child_running = false;
            child_path[0] = '\0';
        }

        // Drain key queue
        KITT::generic_key_t key;
        bool pressed;
        while (kitt.get_key_event(&key, &pressed)) {
            if (!pressed)        continue;
            if (child_running)   continue;

            last_input_ms = millis();

            if (screen == SCR_ABOUT || screen == SCR_SYSINFO) {
                screen = SCR_DESKTOP;
            } else if (screen == SCR_MENU) {
                handle_menu(key);
            } else {
                handle_desktop(key);
            }
        }

        // Redraw
        if (!child_running) {
            switch (screen) {
                case SCR_DESKTOP: draw_desktop(); break;
                case SCR_MENU:    draw_menu();    break;
                case SCR_ABOUT:   draw_about();   break;
                case SCR_SYSINFO: draw_sysinfo(); break;
            }
        }

        // Adaptive refresh rate
        uint32_t age      = millis() - last_input_ms;
        uint32_t delay_ms = (age < IDLE_AFTER_MS) ? REFRESH_ACTIVE_MS : REFRESH_IDLE_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void smol_start() {
    xTaskCreatePinnedToCore(smol_task, "smol", 4096, nullptr, 2, nullptr, 1);
}
