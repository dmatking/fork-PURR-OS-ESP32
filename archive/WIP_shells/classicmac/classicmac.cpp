// classicmac.cpp — Mac System 7/8 Platinum desktop shell (LVGL)
// Ported from Userland/apps/ClassicMac.meow/main.py (v0.2.0 MicroPython original).
// Spec: PURR_OS_docs/08_MacSystem75_UI_Spec.md

#ifdef PURR_HAS_CLASSICMAC
#ifdef PURR_HAS_LVGL

#include "classicmac.h"
#include "purr_wm.h"
#include "../kitt.h"
#include "../purr_version.h"
#include <lvgl.h>
#include "../purr_idf_compat.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

extern KITT kitt;

// ── Platinum colour palette (Mac System 7.5 / 8) ─────────────────────────
#define C_MENUBAR    0xFFFFFF   // white menu bar
#define C_MENUBAR_BD 0x000000   // 1px bottom border
#define C_DESK       0xC0C0C0   // platinum gray desktop
#define C_WIN_BG     0xFFFFFF   // window interior
#define C_WIN_BORDER 0x000000   // window border
#define C_TITLEBAR   0xFFFFFF   // title bar base (pinstripe drawn over)
#define C_PINSTRIPE  0x808080   // alternating stripe colour
#define C_SEL_BG     0x000080   // selected menu item bg (Platinum dark blue)
#define C_SEL_TXT    0xFFFFFF   // selected menu item text
#define C_TEXT       0x000000   // normal text
#define C_DIM        0x808080   // disabled / dim text
#define C_CLOSE_BOX  0xFFFFFF   // close box fill
#define C_APPLE      0x000000   // apple menu symbol

// ── Layout ────────────────────────────────────────────────────────────────
#define SCR_W      320
#define SCR_H      240
#define MB_H        20   // menu bar height
#define DESK_Y      MB_H
#define DESK_H     (SCR_H - MB_H)
#define WIN_TH      14   // title bar height
#define ROW_H       16   // menu row height

// ── Menu state ────────────────────────────────────────────────────────────
static bool     s_purr_open  = false;
static bool     s_apps_open  = false;
static int      s_menu_sel   = -1;
static char     s_active_app[32] = "";

// ── App list ──────────────────────────────────────────────────────────────
#define MAX_APPS 16
static struct { char name[32]; char path[64]; } s_apps[MAX_APPS];
static int s_napp = 0;

static void load_apps() {
    s_napp = 0;
    int n = kitt.app_list_count();
    for (int i = 0; i < n && s_napp < MAX_APPS; i++) {
        KITT::app_entry_t e;
        kitt.app_get_entry(i, &e);
        strncpy(s_apps[s_napp].name, e.name, 31);
        strncpy(s_apps[s_napp].path, e.path, 63);
        s_napp++;
    }
}

// ── LVGL objects ──────────────────────────────────────────────────────────
static lv_obj_t* s_root         = nullptr;
static lv_obj_t* s_menubar      = nullptr;
static lv_obj_t* s_active_lbl   = nullptr;
static lv_obj_t* s_purr_menu    = nullptr;
static lv_obj_t* s_apps_menu    = nullptr;
static lv_obj_t* s_clock_lbl    = nullptr;

// ── Close/hide menus ──────────────────────────────────────────────────────
static void close_menus() {
    if (s_purr_menu) { lv_obj_add_flag(s_purr_menu, LV_OBJ_FLAG_HIDDEN); s_purr_open = false; }
    if (s_apps_menu) { lv_obj_add_flag(s_apps_menu, LV_OBJ_FLAG_HIDDEN); s_apps_open = false; }
    s_menu_sel = -1;
}

// ── Build dropdown ────────────────────────────────────────────────────────
static lv_obj_t* build_dropdown(lv_obj_t* parent, int x,
                                  const char** items, int n_items) {
    int w = 160;
    int h = n_items * ROW_H + 4;

    lv_obj_t* menu = lv_obj_create(parent);
    lv_obj_set_size(menu, w, h);
    lv_obj_set_pos(menu, x, MB_H);
    lv_obj_set_style_bg_color(menu, lv_color_hex(C_WIN_BG), 0);
    lv_obj_set_style_border_color(menu, lv_color_hex(C_WIN_BORDER), 0);
    lv_obj_set_style_border_width(menu, 1, 0);
    lv_obj_set_style_radius(menu, 0, 0);
    lv_obj_set_style_pad_all(menu, 0, 0);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < n_items; i++) {
        lv_obj_t* row = lv_obj_create(menu);
        lv_obj_set_size(row, w - 2, ROW_H);
        lv_obj_set_pos(row, 1, 2 + i * ROW_H);
        lv_obj_set_style_bg_color(row, lv_color_hex(C_WIN_BG), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);

        // Separator (starts with "---")
        if (strncmp(items[i], "---", 3) == 0) {
            lv_obj_t* sep = lv_obj_create(row);
            lv_obj_set_size(sep, w - 4, 1);
            lv_obj_align(sep, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_bg_color(sep, lv_color_hex(C_DIM), 0);
            lv_obj_set_style_border_width(sep, 0, 0);
            continue;
        }

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, items[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_TEXT), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            lv_obj_t* row = lv_event_get_target(e);
            lv_obj_t* lbl = lv_obj_get_child(row, 0);
            if (lbl) {
                const char* txt = lv_label_get_text(lbl);
                Serial.printf("[mac] menu: %s\n", txt);
                if (strcmp(txt, "About PURR OS") == 0) {
                    purr_wm_notify("PURR OS v" PURR_OS_VERSION " — ClassicMac shell", 3000);
                } else if (strcmp(txt, "BlackberryUI") == 0) {
                    purr_wm_switch_shell(PURR_SHELL_BLACKBERRY);
                } else if (strcmp(txt, "Explorer") == 0) {
                    purr_wm_switch_shell(PURR_SHELL_EXPLORER);
                }
            }
            close_menus();
        }, LV_EVENT_CLICKED, nullptr);

        // Hover highlight
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            lv_obj_t* r = lv_event_get_target(e);
            lv_obj_set_style_bg_color(r, lv_color_hex(C_SEL_BG), 0);
            lv_obj_t* l = lv_obj_get_child(r, 0);
            if (l) lv_obj_set_style_text_color(l, lv_color_hex(C_SEL_TXT), 0);
        }, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            lv_obj_t* r = lv_event_get_target(e);
            lv_obj_set_style_bg_color(r, lv_color_hex(C_WIN_BG), 0);
            lv_obj_t* l = lv_obj_get_child(r, 0);
            if (l) lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
        }, LV_EVENT_DEFOCUSED, nullptr);
    }

    return menu;
}

// ── Build Mac-style window ────────────────────────────────────────────────
static lv_obj_t* build_mac_window(lv_obj_t* parent,
                                    const char* title,
                                    int x, int y, int w, int h) {
    lv_obj_t* win = lv_obj_create(parent);
    lv_obj_set_size(win, w, h);
    lv_obj_set_pos(win, x, y);
    lv_obj_set_style_bg_color(win, lv_color_hex(C_WIN_BG), 0);
    lv_obj_set_style_border_color(win, lv_color_hex(C_WIN_BORDER), 0);
    lv_obj_set_style_border_width(win, 1, 0);
    lv_obj_set_style_radius(win, 0, 0);
    lv_obj_set_style_pad_all(win, 0, 0);

    // Title bar with pinstripes
    lv_obj_t* titlebar = lv_obj_create(win);
    lv_obj_set_size(titlebar, w - 2, WIN_TH);
    lv_obj_set_pos(titlebar, 1, 1);
    lv_obj_set_style_bg_color(titlebar, lv_color_hex(C_TITLEBAR), 0);
    lv_obj_set_style_border_width(titlebar, 0, 0);
    lv_obj_set_style_radius(titlebar, 0, 0);
    lv_obj_set_style_pad_all(titlebar, 0, 0);

    // Pinstripe lines (every 2px)
    for (int sy = 2; sy < WIN_TH; sy += 2) {
        lv_obj_t* stripe = lv_obj_create(titlebar);
        lv_obj_set_size(stripe, w - 2, 1);
        lv_obj_set_pos(stripe, 0, sy);
        lv_obj_set_style_bg_color(stripe, lv_color_hex(C_PINSTRIPE), 0);
        lv_obj_set_style_border_width(stripe, 0, 0);
        lv_obj_set_style_radius(stripe, 0, 0);
    }

    // Close box (top-left 9x9)
    lv_obj_t* close_box = lv_obj_create(titlebar);
    lv_obj_set_size(close_box, 9, 9);
    lv_obj_set_pos(close_box, 3, (WIN_TH - 9) / 2);
    lv_obj_set_style_bg_color(close_box, lv_color_hex(C_CLOSE_BOX), 0);
    lv_obj_set_style_border_color(close_box, lv_color_hex(C_WIN_BORDER), 0);
    lv_obj_set_style_border_width(close_box, 1, 0);
    lv_obj_set_style_radius(close_box, 0, 0);

    lv_obj_add_event_cb(close_box, [](lv_event_t* e) {
        lv_obj_t* box   = lv_event_get_target(e);
        lv_obj_t* tbar  = lv_obj_get_parent(box);
        lv_obj_t* win   = lv_obj_get_parent(tbar);
        lv_obj_del(win);
    }, LV_EVENT_CLICKED, nullptr);

    // Title label
    lv_obj_t* title_lbl = lv_label_create(titlebar);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(C_TEXT), 0);
    lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, 0);

    // Separator below title bar
    lv_obj_t* sep = lv_obj_create(win);
    lv_obj_set_size(sep, w - 2, 1);
    lv_obj_set_pos(sep, 1, WIN_TH + 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(C_WIN_BORDER), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);

    return win;
}

// ── Shell create function ─────────────────────────────────────────────────
static lv_obj_t* classicmac_shell_create(lv_obj_t* parent) {
    load_apps();
    s_root = parent;
    Serial.printf("[mac] creating shell  apps=%d\n", s_napp);

    // Desktop (Platinum gray)
    lv_obj_set_style_bg_color(parent, lv_color_hex(C_DESK), 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    // Menu bar
    s_menubar = lv_obj_create(parent);
    lv_obj_set_size(s_menubar, SCR_W, MB_H);
    lv_obj_set_pos(s_menubar, 0, 0);
    lv_obj_set_style_bg_color(s_menubar, lv_color_hex(C_MENUBAR), 0);
    lv_obj_set_style_border_width(s_menubar, 0, 0);
    lv_obj_set_style_radius(s_menubar, 0, 0);
    lv_obj_set_style_pad_all(s_menubar, 0, 0);
    // Bottom border
    lv_obj_t* mb_border = lv_obj_create(s_menubar);
    lv_obj_set_size(mb_border, SCR_W, 1);
    lv_obj_set_pos(mb_border, 0, MB_H - 1);
    lv_obj_set_style_bg_color(mb_border, lv_color_hex(C_MENUBAR_BD), 0);
    lv_obj_set_style_border_width(mb_border, 0, 0);

    // PURR menu button (Apple logo substitute)
    lv_obj_t* purr_btn = lv_obj_create(s_menubar);
    lv_obj_set_size(purr_btn, 36, MB_H - 1);
    lv_obj_set_pos(purr_btn, 0, 0);
    lv_obj_set_style_bg_color(purr_btn, lv_color_hex(C_MENUBAR), 0);
    lv_obj_set_style_border_width(purr_btn, 0, 0);
    lv_obj_set_style_radius(purr_btn, 0, 0);
    lv_obj_set_style_pad_all(purr_btn, 0, 0);
    lv_obj_t* purr_lbl = lv_label_create(purr_btn);
    lv_label_set_text(purr_lbl, "PURR");
    lv_obj_set_style_text_color(purr_lbl, lv_color_hex(C_TEXT), 0);
    lv_obj_align(purr_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(purr_btn, [](lv_event_t*) {
        if (s_apps_open) close_menus();
        s_purr_open = !s_purr_open;
        if (s_purr_menu) {
            if (s_purr_open) lv_obj_clear_flag(s_purr_menu, LV_OBJ_FLAG_HIDDEN);
            else             lv_obj_add_flag(s_purr_menu,   LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Apps menu button
    lv_obj_t* apps_btn = lv_obj_create(s_menubar);
    lv_obj_set_size(apps_btn, 36, MB_H - 1);
    lv_obj_set_pos(apps_btn, 38, 0);
    lv_obj_set_style_bg_color(apps_btn, lv_color_hex(C_MENUBAR), 0);
    lv_obj_set_style_border_width(apps_btn, 0, 0);
    lv_obj_set_style_radius(apps_btn, 0, 0);
    lv_obj_set_style_pad_all(apps_btn, 0, 0);
    lv_obj_t* apps_lbl = lv_label_create(apps_btn);
    lv_label_set_text(apps_lbl, "Apps");
    lv_obj_set_style_text_color(apps_lbl, lv_color_hex(C_TEXT), 0);
    lv_obj_align(apps_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(apps_btn, [](lv_event_t*) {
        if (s_purr_open) close_menus();
        s_apps_open = !s_apps_open;
        if (s_apps_menu) {
            if (s_apps_open) lv_obj_clear_flag(s_apps_menu, LV_OBJ_FLAG_HIDDEN);
            else             lv_obj_add_flag(s_apps_menu,   LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Active app name (center of menu bar)
    s_active_lbl = lv_label_create(s_menubar);
    lv_label_set_text(s_active_lbl, "Finder");
    lv_obj_set_style_text_color(s_active_lbl, lv_color_hex(C_TEXT), 0);
    lv_obj_align(s_active_lbl, LV_ALIGN_CENTER, 0, 0);

    // Clock (right side)
    s_clock_lbl = lv_label_create(s_menubar);
    lv_label_set_text(s_clock_lbl, "00:00");
    lv_obj_set_style_text_color(s_clock_lbl, lv_color_hex(C_TEXT), 0);
    lv_obj_align(s_clock_lbl, LV_ALIGN_RIGHT_MID, -6, 0);

    // PURR dropdown
    static const char* purr_items[] = {
        "About PURR OS",
        "---",
        "BlackberryUI",
        "Explorer",
        "---",
        "System Info",
    };
    s_purr_menu = build_dropdown(parent, 0, purr_items, 6);

    // Apps dropdown (dynamically built from app list)
    // Build a static list for now with first MAX_APPS entries
    static char   app_labels[MAX_APPS][32];
    static const char* app_ptrs[MAX_APPS];
    for (int i = 0; i < s_napp; i++) {
        strncpy(app_labels[i], s_apps[i].name, 31);
        app_ptrs[i] = app_labels[i];
    }
    s_apps_menu = build_dropdown(parent, 38, app_ptrs, s_napp);

    // Welcome window on desktop
    lv_obj_t* welcome = build_mac_window(parent, "PURR OS — Finder",
                                          40, DESK_Y + 20, 240, 120);
    lv_obj_t* body = lv_label_create(welcome);
    lv_label_set_text(body,
        "Welcome to PURR OS\n"
        "v" PURR_OS_VERSION "\n\n"
        "Use the menu bar to\n"
        "launch apps.");
    lv_obj_set_style_text_color(body, lv_color_hex(C_TEXT), 0);
    lv_obj_set_pos(body, 8, WIN_TH + 8);

    // Tap outside menus → close
    lv_obj_add_event_cb(parent, [](lv_event_t* e) {
        if (s_purr_open || s_apps_open) close_menus();
    }, LV_EVENT_CLICKED, nullptr);

    return parent;
}

// ── Update task (clock) ───────────────────────────────────────────────────
static void mac_update_task(void*) {
    for (;;) {
        if (s_clock_lbl) {
            uint32_t sec = millis() / 1000;
            char tbuf[10];
            snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu",
                     (unsigned long)(sec / 60), (unsigned long)(sec % 60));
            lv_label_set_text(s_clock_lbl, tbuf);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ── Public entry point ─────────────────────────────────────────────────────
extern "C" void classicmac_start() {
    purr_wm_register_shell(PURR_SHELL_CLASSICMAC, classicmac_shell_create);
    xTaskCreatePinnedToCore(mac_update_task, "mac_tick", 2048, nullptr, 1, nullptr, 1);
    Serial.println("[mac] shell registered");
}

#endif  // PURR_HAS_LVGL
#endif  // PURR_HAS_CLASSICMAC
