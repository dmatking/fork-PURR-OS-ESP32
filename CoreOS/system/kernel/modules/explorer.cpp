// explorer.cpp — PURR OS Explorer shell (LVGL)
// Windows CE-inspired: left shortcut strip, app icon grid, silver taskbar.

#ifdef PURR_HAS_EXPLORER
#ifdef PURR_HAS_LVGL

#include "explorer.h"
#include "purr_wm.h"
#include "../kitt.h"
#include "../purr_version.h"
#include <lvgl.h>
#include "../purr_idf_compat.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdio.h>

extern KITT kitt;

// ── Colours ───────────────────────────────────────────────────────────────
#define C_DESKTOP   0x082062
#define C_TASKBAR   0xC0C0C0
#define C_TASKTEXT  0x000000
#define C_START     0x006000
#define C_STRIP     0x101820
#define C_STRIPBDR  0x203040
#define C_ICONTXT   0xFFFFFF
#define C_WATERMARK 0x103060
#define C_MYPC      0x1C4E7F
#define C_LOGS      0x7F3010
#define C_SD        0x0F2848

static const uint32_t APP_COLORS[] = {
    0x1C4E7F, 0x4E2000, 0x404000, 0x006000,
    0x4E1010, 0x1C1F4C, 0x3C2F1E, 0x003E3E,
};
static const int N_APP_COLORS = (int)(sizeof(APP_COLORS) / sizeof(APP_COLORS[0]));

// ── Layout ────────────────────────────────────────────────────────────────
#define SCR_W     320
#define SCR_H     240
#define TASKBAR_H  28
#define DESKTOP_H (SCR_H - TASKBAR_H)
#define STRIP_W    50
#define GRID_X     STRIP_W
#define GRID_W    (SCR_W - STRIP_W)
#define GRID_COLS  3
#define GRID_ROWS  3
#define CELL_W    (GRID_W / GRID_COLS)
#define CELL_H    (DESKTOP_H / GRID_ROWS)

// ── App grid ──────────────────────────────────────────────────────────────
#define MAX_GRID (GRID_ROWS * GRID_COLS)
struct AppIcon { char label[32]; char path[64]; uint32_t color; };
static AppIcon s_grid[MAX_GRID];
static int     s_ngrid = 0;

static void build_grid() {
    memset(s_grid, 0, sizeof(s_grid));
    s_ngrid = 0;
    int n = kitt.app_list_count();
    for (int i = 0; i < n && s_ngrid < MAX_GRID; i++) {
        KITT::app_entry_t e;
        kitt.app_get_entry(i, &e);
        strncpy(s_grid[s_ngrid].label, e.name, 31);
        strncpy(s_grid[s_ngrid].path,  e.path, 63);
        s_grid[s_ngrid].color = APP_COLORS[i % N_APP_COLORS];
        s_ngrid++;
    }
}

// ── Shell create function ─────────────────────────────────────────────────
static lv_obj_t* explorer_shell_create(lv_obj_t* parent) {
    build_grid();
    Serial.printf("[exp] creating shell  apps=%d\n", s_ngrid);

    lv_obj_set_style_bg_color(parent, lv_color_hex(C_DESKTOP), 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    // ── Left shortcut strip ───────────────────────────────────────────────
    lv_obj_t* strip = lv_obj_create(parent);
    lv_obj_set_size(strip, STRIP_W, DESKTOP_H);
    lv_obj_set_pos(strip, 0, 0);
    lv_obj_set_style_bg_color(strip, lv_color_hex(C_STRIP), 0);
    lv_obj_set_style_border_color(strip, lv_color_hex(C_STRIPBDR), 0);
    lv_obj_set_style_border_width(strip, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(strip, 0, 0);
    lv_obj_set_style_pad_all(strip, 0, 0);

    static const struct { const char* label; uint32_t color; } STRIP_ITEMS[3] = {
        { "CYD",  C_MYPC },
        { "Logs", C_LOGS },
        { "SD",   C_SD   },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn = lv_obj_create(strip);
        lv_obj_set_size(btn, 36, 28);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 10 + i * 60);
        lv_obj_set_style_bg_color(btn, lv_color_hex(STRIP_ITEMS[i].color), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x304050), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, STRIP_ITEMS[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_ICONTXT), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int* pi = (int*)lv_event_get_user_data(e);
            Serial.printf("[exp] strip[%d] tapped\n", *pi);
        }, LV_EVENT_CLICKED, new int(i));
    }

    // Watermark in strip
    lv_obj_t* wmark = lv_label_create(strip);
    lv_label_set_text(wmark, "PURR");
    lv_obj_set_style_text_color(wmark, lv_color_hex(C_WATERMARK), 0);
    lv_obj_align(wmark, LV_ALIGN_BOTTOM_MID, 0, -4);

    // ── App icon grid ─────────────────────────────────────────────────────
    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int idx = row * GRID_COLS + col;
            int16_t cx = GRID_X + col * CELL_W;
            int16_t cy = row * CELL_H;

            lv_obj_t* cell = lv_obj_create(parent);
            lv_obj_set_size(cell, CELL_W - 2, CELL_H - 2);
            lv_obj_set_pos(cell, cx + 1, cy + 1);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);

            if (idx < s_ngrid) {
                lv_obj_set_style_bg_color(cell, lv_color_hex(s_grid[idx].color), 0);

                // Icon box
                lv_obj_t* icon = lv_obj_create(cell);
                lv_obj_set_size(icon, 40, 32);
                lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 4);
                lv_obj_set_style_bg_color(icon, lv_color_hex(s_grid[idx].color), 0);
                lv_obj_set_style_border_color(icon, lv_color_hex(0x304060), 0);
                lv_obj_set_style_border_width(icon, 1, 0);
                lv_obj_set_style_radius(icon, 4, 0);
                lv_obj_set_style_pad_all(icon, 0, 0);

                char abbr[3] = { s_grid[idx].label[0],
                                 s_grid[idx].label[1] ? s_grid[idx].label[1] : ' ', '\0' };
                lv_obj_t* abbr_lbl = lv_label_create(icon);
                lv_label_set_text(abbr_lbl, abbr);
                lv_obj_set_style_text_color(abbr_lbl, lv_color_hex(0xFFFFFF), 0);
                lv_obj_center(abbr_lbl);

                lv_obj_t* name_lbl = lv_label_create(cell);
                lv_label_set_text(name_lbl, s_grid[idx].label);
                lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_CLIP);
                lv_obj_set_width(name_lbl, CELL_W - 4);
                lv_obj_align(name_lbl, LV_ALIGN_BOTTOM_MID, 0, -2);
                lv_obj_set_style_text_color(name_lbl, lv_color_hex(C_ICONTXT), 0);

                lv_obj_add_event_cb(cell, [](lv_event_t* e) {
                    int* pi = (int*)lv_event_get_user_data(e);
                    if (*pi < s_ngrid) purr_wm_launch(s_grid[*pi].path);
                }, LV_EVENT_CLICKED, new int(idx));
            } else {
                lv_obj_set_style_bg_color(cell, lv_color_hex(0x0A1530), 0);
            }
        }
    }

    // ── Taskbar ───────────────────────────────────────────────────────────
    lv_obj_t* taskbar = lv_obj_create(parent);
    lv_obj_set_size(taskbar, SCR_W, TASKBAR_H);
    lv_obj_set_pos(taskbar, 0, DESKTOP_H);
    lv_obj_set_style_bg_color(taskbar, lv_color_hex(C_TASKBAR), 0);
    lv_obj_set_style_border_width(taskbar, 0, 0);
    lv_obj_set_style_radius(taskbar, 0, 0);
    lv_obj_set_style_pad_all(taskbar, 0, 0);

    // Start button
    lv_obj_t* start = lv_obj_create(taskbar);
    lv_obj_set_size(start, 54, TASKBAR_H - 4);
    lv_obj_align(start, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(start, lv_color_hex(C_START), 0);
    lv_obj_set_style_border_width(start, 1, 0);
    lv_obj_set_style_border_color(start, lv_color_hex(0x004000), 0);
    lv_obj_set_style_radius(start, 3, 0);
    lv_obj_set_style_pad_all(start, 0, 0);

    lv_obj_t* start_lbl = lv_label_create(start);
    lv_label_set_text(start_lbl, "Start");
    lv_obj_set_style_text_color(start_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(start_lbl);

    // Clock
    lv_obj_t* clock_lbl = lv_label_create(taskbar);
    uint32_t sec = millis() / 1000;
    char tbuf[10];
    snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu",
             (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    lv_label_set_text(clock_lbl, tbuf);
    lv_obj_set_style_text_color(clock_lbl, lv_color_hex(C_TASKTEXT), 0);
    lv_obj_align(clock_lbl, LV_ALIGN_RIGHT_MID, -6, 0);

    // Shell switcher — tap taskbar watermark to switch to BlackberryUI
    lv_obj_t* switcher = lv_label_create(taskbar);
    lv_label_set_text(switcher, "PURR OS " PURR_OS_VERSION);
    lv_obj_set_style_text_color(switcher, lv_color_hex(0x404040), 0);
    lv_obj_align(switcher, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(switcher, [](lv_event_t*) {
        purr_wm_switch_shell(PURR_SHELL_BLACKBERRY);
    }, LV_EVENT_CLICKED, nullptr);

    return parent;
}

// ── Public entry point ─────────────────────────────────────────────────────
extern "C" void explorer_start() {
    purr_wm_register_shell(PURR_SHELL_EXPLORER, explorer_shell_create);
    Serial.println("[exp] shell registered");
}

#endif  // PURR_HAS_LVGL
#endif  // PURR_HAS_EXPLORER
