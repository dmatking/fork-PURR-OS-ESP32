// explorer.cpp — PURR OS default app launcher (explorer.paws)
// Windows CE-inspired: left shortcut strip, 3x3 app icon grid, silver taskbar.

#ifdef PURR_HAS_EXPLORER

#include "explorer.h"
#include "purr_bootloader.h"
#include "display_ili9341.h"
#include "touch_cst816s.h"
#include "../kitt.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdio.h>

extern KITT kitt;

// ── Colour palette ────────────────────────────────────────────────────────────
static const uint16_t C_DESKTOP  = 0x0862;  // dark teal-gray desktop
static const uint16_t C_TASKBAR  = 0xC618;  // silver taskbar
static const uint16_t C_TASKTEXT = 0x0000;  // black on taskbar
static const uint16_t C_START    = 0x0300;  // dark green start button
static const uint16_t C_STARTLBL = 0xFFFF;
static const uint16_t C_STRIP    = 0x0841;  // left strip (slightly lighter)
static const uint16_t C_STRIPBDR = 0x4208;  // strip right border
static const uint16_t C_ICONTXT  = 0xFFFF;  // white icon labels
static const uint16_t C_SHADOW   = 0x2104;  // icon shadow border
static const uint16_t C_DIV      = 0x4A49;  // taskbar top shadow
static const uint16_t C_WATERMARK= 0x10C3;  // faint PURR OS watermark


// Strip icon colours
static const uint16_t C_MYPC     = 0x3CDF;
static const uint16_t C_LOGS     = 0xF186;
static const uint16_t C_SD       = 0x2E48;

// ── Layout constants ──────────────────────────────────────────────────────────
#define SCR_W       320
#define SCR_H       240
#define TASKBAR_H   27
#define DESKTOP_H   (SCR_H - TASKBAR_H)    // 213
#define TASKBAR_Y   DESKTOP_H

#define STRIP_W     50
#define STRIP_ICON_W 32
#define STRIP_ICON_H 28
#define STRIP_ICON_X ((STRIP_W - STRIP_ICON_W) / 2)  // 9
static const int16_t STRIP_YS[3] = { 14, 84, 154 };

#define GRID_X      STRIP_W
#define GRID_W      (SCR_W - STRIP_W)      // 270
#define GRID_COLS   3
#define GRID_ROWS   3
#define CELL_W      (GRID_W / GRID_COLS)   // 90
#define CELL_H      (DESKTOP_H / GRID_ROWS) // 71
#define ICON_W      40
#define ICON_H      36
#define ICON_OFF_X  ((CELL_W - ICON_W) / 2)  // 25
#define ICON_OFF_Y  7

#define START_W     54

// ── App definitions ───────────────────────────────────────────────────────────
enum IconAction : uint8_t { ACT_LAUNCH = 0, ACT_BOOTLOADER = 1, ACT_ABOUT = 2 };

#define MAX_GRID (GRID_ROWS * GRID_COLS)

struct AppIcon {
    char       label[32];
    uint16_t   color;
    char       path[64];
    IconAction action;
    bool       active;   // false = empty slot, don't render
};

static AppIcon s_grid[MAX_GRID];
static int     s_ngrid = 0;

// Palette for dynamically discovered apps — cycles through distinct colours
static const uint16_t APP_COLORS[] = {
    0x3CDF, 0xFD20, 0xFFE0, 0x801F, 0x07E0, 0x4C9F, 0xF7BE, 0x07FF,
};
static const int N_APP_COLORS = (int)(sizeof(APP_COLORS) / sizeof(APP_COLORS[0]));

static void build_grid() {
    memset(s_grid, 0, sizeof(s_grid));
    s_ngrid = 0;

    // Slot 0: Flash OS — always pinned
    auto& fl = s_grid[s_ngrid++];
    strncpy(fl.label, "Flash OS", sizeof(fl.label));
    fl.color  = 0xF800;
    fl.path[0] = '\0';
    fl.action = ACT_BOOTLOADER;
    fl.active = true;

    // Remaining slots: filled from kitt app scan
    int n = kitt.app_list_count();
    for (int i = 0; i < n && s_ngrid < MAX_GRID; i++) {
        KITT::app_entry_t e;
        kitt.app_get_entry(i, &e);
        auto& g = s_grid[s_ngrid++];
        strncpy(g.label, e.name,  sizeof(g.label));
        strncpy(g.path,  e.path,  sizeof(g.path));
        g.color  = APP_COLORS[i % N_APP_COLORS];
        g.action = ACT_LAUNCH;
        g.active = true;
    }
}

static const struct { const char* label; uint16_t color; }
STRIP[3] = {
    { "My CYD", C_MYPC },
    { "Logs",   C_LOGS },
    { "SD",     C_SD   },
};

// ── Hit table ─────────────────────────────────────────────────────────────────
struct HitZone { int16_t x, y, w, h; int id; };
static HitZone s_hits[16];
static int     s_nhit = 0;

static void hit_reg(int16_t x, int16_t y, int16_t w, int16_t h, int id) {
    if (s_nhit < 16) s_hits[s_nhit++] = {x, y, w, h, id};
}
static int hit_test(int16_t tx, int16_t ty) {
    for (int i = 0; i < s_nhit; i++) {
        const HitZone& z = s_hits[i];
        if (tx >= z.x && tx < z.x+z.w && ty >= z.y && ty < z.y+z.h)
            return z.id;
    }
    return -2;  // miss
}

// ── Draw helpers ──────────────────────────────────────────────────────────────
static inline void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    display_ili9341_fill_rect(x, y, w, h, c);
}
static inline void hline(int16_t x, int16_t y, int16_t w, uint16_t c) {
    display_ili9341_draw_hline(x, y, w, c);
}
static inline void str(int16_t x, int16_t y, const char* s,
                       uint16_t fg, uint16_t bg, uint8_t sz) {
    display_ili9341_draw_string(x, y, s, fg, bg, sz);
}
static void str_cx(int16_t bx, int16_t by, int16_t bw, int16_t bh,
                   const char* s, uint16_t fg, uint16_t bg, uint8_t sz = 1) {
    int16_t tw = (int16_t)(strlen(s) * 6 * sz);
    int16_t tx = bx + (bw - tw) / 2;
    int16_t ty = by + (bh - 8 * sz) / 2;
    str(tx, ty, s, fg, bg, sz);
}

// ── Render ────────────────────────────────────────────────────────────────────
static void render_desktop() {
    s_nhit = 0;

    // Desktop background
    fill(0, 0, SCR_W, DESKTOP_H, C_DESKTOP);

    // "PURR OS" watermark — large faint text centred in grid area
    {
        const char* wm = "PURR OS";
        int16_t tw = (int16_t)(strlen(wm) * 18);  // size 3 = 6*3 px/char
        int16_t wx = GRID_X + (GRID_W - tw) / 2;
        int16_t wy = DESKTOP_H / 2 - 12;
        str(wx, wy, wm, C_WATERMARK, C_DESKTOP, 3);
    }

    // ── Left strip ────────────────────────────────────────────────────────────
    fill(0, 0, STRIP_W, DESKTOP_H, C_STRIP);
    fill(STRIP_W - 1, 0, 1, DESKTOP_H, C_STRIPBDR);

    for (int i = 0; i < 3; i++) {
        int16_t sy = STRIP_YS[i];
        fill(STRIP_ICON_X, sy, STRIP_ICON_W, STRIP_ICON_H, STRIP[i].color);
        str_cx(0, sy + STRIP_ICON_H + 2, STRIP_W, 10, STRIP[i].label, C_ICONTXT, C_STRIP);
        hit_reg(0, sy, STRIP_W, STRIP_ICON_H + 12, -10 - i);
    }

    // ── App icon grid ─────────────────────────────────────────────────────────
    for (int i = 0; i < s_ngrid; i++) {
        const AppIcon& app = s_grid[i];
        if (!app.active) continue;

        int row = i / GRID_COLS;
        int col = i % GRID_COLS;
        int16_t cx = GRID_X + col * CELL_W;
        int16_t cy = row * CELL_H;

        // Shadow border
        fill(cx + ICON_OFF_X - 1, cy + ICON_OFF_Y - 1,
             ICON_W + 2, ICON_H + 2, C_SHADOW);
        // Tile
        fill(cx + ICON_OFF_X, cy + ICON_OFF_Y, ICON_W, ICON_H, app.color);
        // Label
        str_cx(cx, cy + ICON_OFF_Y + ICON_H + 4, CELL_W, 10,
               app.label, C_ICONTXT, C_DESKTOP);
        // Hit zone
        hit_reg(cx, cy, CELL_W, CELL_H, i);
    }
}

static void render_taskbar() {
    hline(0, TASKBAR_Y, SCR_W, C_DIV);
    fill(0, TASKBAR_Y + 1, SCR_W, TASKBAR_H - 1, C_TASKBAR);

    // Start button
    fill(2, TASKBAR_Y + 3, START_W, TASKBAR_H - 6, C_START);
    str_cx(2, TASKBAR_Y + 3, START_W, TASKBAR_H - 6, "PURR", C_STARTLBL, C_START);
    hit_reg(2, TASKBAR_Y + 3, START_W, TASKBAR_H - 6, -1);

    // Device name
    str(START_W + 8, TASKBAR_Y + 9, kitt.device_name(), C_TASKTEXT, C_TASKBAR, 1);

    // RAM free right-aligned
    KITT::memory_stats_t mem;
    kitt.memory_get_stats(&mem);
    char ram[20];
    snprintf(ram, sizeof(ram), "%luK free", (unsigned long)mem.free_ram_kb);
    int16_t rx = SCR_W - (int16_t)(strlen(ram) * 6) - 4;
    str(rx, TASKBAR_Y + 9, ram, C_TASKTEXT, C_TASKBAR, 1);
}

// ── Touch / action ────────────────────────────────────────────────────────────
static void on_tap(int id) {
    if (id == -1) {
        // Start button — future: show start menu
        Serial.println("[explorer] start tapped");
        return;
    }
    if (id <= -10) {
        Serial.printf("[explorer] strip icon %d\n", -id - 10);
        return;
    }
    if (id >= 0 && id < s_ngrid) {
        const AppIcon& app = s_grid[id];
        Serial.printf("[explorer] launch: %s\n", app.label);
        switch (app.action) {
        case ACT_BOOTLOADER:
            purr_bootloader_request_reboot();
            break;
        case ACT_LAUNCH:
            if (app.path[0]) kitt.app_launch(app.path);
            break;
        case ACT_ABOUT:
            // TODO: about overlay
            break;
        }
    }
}

static bool     s_prev_pressed = false;
static uint32_t s_debounce_ms  = 0;

static void handle_touch() {
    cst_touch_event_t ev;
    bool pressed = touch_cst816s_get_event(&ev) && ev.pressed;
    if (pressed && !s_prev_pressed) {
        uint32_t now = millis();
        if (now - s_debounce_ms >= 300) {
            s_debounce_ms = now;
            int id = hit_test(ev.x, ev.y);
            if (id != -2) on_tap(id);
        }
    }
    s_prev_pressed = pressed;
}

// ── LED heartbeat (green = normal mode) ──────────────────────────────────────
#define CYD_LED_R  4
#define CYD_LED_G 16
#define CYD_LED_B 17

static void led_init() {
    pinMode(CYD_LED_R, OUTPUT); digitalWrite(CYD_LED_R, HIGH);
    pinMode(CYD_LED_G, OUTPUT); digitalWrite(CYD_LED_G, HIGH);
    pinMode(CYD_LED_B, OUTPUT); digitalWrite(CYD_LED_B, HIGH);
}

static void led_heartbeat() {
    static uint32_t s_ms = 0;
    static bool     s_on = false;
    if (millis() - s_ms >= 1000) {
        s_ms = millis(); s_on = !s_on;
        digitalWrite(CYD_LED_G, s_on ? LOW : HIGH);
    }
}

// ── Main task ─────────────────────────────────────────────────────────────────
static void explorer_task(void*) {
    led_init();
    build_grid();   // populate from kitt.app_list() + pinned Flash OS
    render_desktop();
    render_taskbar();
    Serial.printf("[explorer] ready  apps=%d\n", s_ngrid);

    static uint32_t s_tick = 0;
    while (true) {
        handle_touch();

        // Refresh taskbar RAM every 5s
        if (millis() - s_tick >= 5000) {
            s_tick = millis();
            render_taskbar();
        }

        led_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void explorer_start() {
    xTaskCreatePinnedToCore(explorer_task, "explorer", 8192, nullptr, 3, nullptr, 1);
}

#endif  // PURR_HAS_EXPLORER
