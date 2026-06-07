// blackberry_ui.cpp — BlackberryUI shell (LVGL)
// BB OS 6-inspired homescreen for PURR OS.
// Renders via LVGL widgets — no MiniWin dependency.

#ifdef PURR_HAS_BLACKBERRY_UI
#ifdef PURR_HAS_LVGL

#include "blackberry_ui.h"
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

#ifdef PURR_BBUI_TARGET_TOUCH
#  define CYD_LED_R  4
#  define CYD_LED_G  16
#  define CYD_LED_B  17
#endif

// ── Colour palette (LVGL hex) ─────────────────────────────────────────────
#define C_STATUS_BG    0x000000
#define C_STATUS_TXT   0xFFFFFF
#define C_TIME_BG      0x063163
#define C_TIME_TXT     0xFFFFFF
#define C_TIME_SUB     0x848484
#define C_NOTIF_BG     0x101820
#define C_NOTIF_TXT    0xFFFFFF
#define C_WALL_BG      0x063163
#define C_WALL_PLATE   0x041040
#define C_WATERMARK    0x142548
#define C_HINT_TXT     0x202020
#define C_TAB_BG       0x080808
#define C_TAB_ACTIVE   0xFFFFFF
#define C_TAB_INACT    0x505890
#define C_DOCK_BG      0x080810
#define C_ICON_BG      0x183060
#define C_ICON_BORDER  0x203060
#define C_ICON_TXT     0xFFFFFF
#define C_DRAWER_BG    0x080808
#define C_DIV          0x202020

static const uint32_t APP_COLORS[] = {
    0x1E2E4E, 0x4E2000, 0x404000, 0x004E00,
    0x4E1010, 0x1C1F4C, 0x3C2F1E, 0x003E3E,
    0x1C3C1F, 0x200180, 0x3C0000, 0x004810,
};
static const int N_APP_COLORS = (int)(sizeof(APP_COLORS) / sizeof(APP_COLORS[0]));

// ── Layout ────────────────────────────────────────────────────────────────
#define SCR_W    320
#define SCR_H    240
#define STATUS_H  20
#define TIME_H    30
#define NOTIF_H   20
#define TAB_H     16
#define DOCK_H    20
#define CONTENT_Y (STATUS_H + TIME_H + NOTIF_H)
#define CONTENT_H (SCR_H - CONTENT_Y - TAB_H - DOCK_H)
#define TAB_Y     (SCR_H - DOCK_H - TAB_H)
#define DOCK_Y    (SCR_H - DOCK_H)
#define DOCK_COLS  4
#define DOCK_SW   (SCR_W / DOCK_COLS)
#define TAB_COUNT  3
#define GRID_COLS  4
#define CELL_W    (SCR_W / GRID_COLS)
#define CELL_H     50

// ── App list ──────────────────────────────────────────────────────────────
#define MAX_APPS 32
struct BbApp { char name[32]; char path[128]; };
static BbApp s_apps[MAX_APPS];
static int   s_napp = 0;

static void scan_apps() {
    s_napp = 0;
    int n = kitt.app_list_count();
    for (int i = 0; i < n && s_napp < MAX_APPS; i++) {
        KITT::app_entry_t e;
        kitt.app_get_entry(i, &e);
        strncpy(s_apps[s_napp].name, e.name, 31);
        strncpy(s_apps[s_napp].path, e.path, 127);
        s_napp++;
    }
}

// ── State ─────────────────────────────────────────────────────────────────
static const char* const TAB_NAMES[TAB_COUNT] = { "Frequent", "All", "Favorites" };
static const char* const DOCK_LABELS[DOCK_COLS] = { "WIFI", "MSGS", "FLASH", "LOGS" };
static const uint32_t    DOCK_COLORS[DOCK_COLS] = { 0x1E2E4E, 0x004E00, 0x4E2000, 0x4E1010 };

static int s_tab = 1;

// LVGL object handles
static lv_obj_t* s_root        = nullptr;
static lv_obj_t* s_home_screen = nullptr;
static lv_obj_t* s_drawer      = nullptr;
static lv_obj_t* s_tab_btns[TAB_COUNT] = {};
static lv_obj_t* s_grid_cont   = nullptr;
static lv_obj_t* s_time_lbl    = nullptr;
static lv_obj_t* s_status_lbl  = nullptr;
static bool      s_drawer_open = false;

// ── LED heartbeat ─────────────────────────────────────────────────────────
#ifdef PURR_BBUI_TARGET_TOUCH
static void led_init() {
    pinMode(CYD_LED_R, OUTPUT); digitalWrite(CYD_LED_R, HIGH);
    pinMode(CYD_LED_G, OUTPUT); digitalWrite(CYD_LED_G, HIGH);
    pinMode(CYD_LED_B, OUTPUT); digitalWrite(CYD_LED_B, HIGH);
}
static void led_heartbeat() {
    static uint32_t ms = 0; static bool on = false;
    if (millis() - ms >= 2000) {
        ms = millis(); on = !on;
        digitalWrite(CYD_LED_B, on ? LOW : HIGH);
    }
}
#endif

// ── Grid populate ─────────────────────────────────────────────────────────
static void grid_populate() {
    if (!s_grid_cont) return;
    lv_obj_clean(s_grid_cont);

    int max_rows = (SCR_H - (STATUS_H + NOTIF_H + TAB_H)) / CELL_H;
    for (int row = 0; row < max_rows; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int idx = row * GRID_COLS + col;

            lv_obj_t* cell = lv_obj_create(s_grid_cont);
            lv_obj_set_size(cell, CELL_W - 2, CELL_H - 2);
            lv_obj_set_pos(cell, col * CELL_W + 1, row * CELL_H + 1);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_radius(cell, 0, 0);

            if (idx < s_napp) {
                uint32_t tc = APP_COLORS[idx % N_APP_COLORS];
                lv_obj_set_style_bg_color(cell, lv_color_hex(tc), 0);

                // Icon (2-letter abbreviation)
                lv_obj_t* icon = lv_obj_create(cell);
                lv_obj_set_size(icon, 44, 28);
                lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 4);
                lv_obj_set_style_bg_color(icon, lv_color_hex(tc), 0);
                lv_obj_set_style_border_color(icon, lv_color_hex(0x304060), 0);
                lv_obj_set_style_border_width(icon, 1, 0);
                lv_obj_set_style_radius(icon, 4, 0);
                lv_obj_set_style_pad_all(icon, 0, 0);

                char abbr[3] = { s_apps[idx].name[0],
                                 s_apps[idx].name[1] ? s_apps[idx].name[1] : ' ', '\0' };
                lv_obj_t* abbr_lbl = lv_label_create(icon);
                lv_label_set_text(abbr_lbl, abbr);
                lv_obj_set_style_text_color(abbr_lbl, lv_color_hex(0xFFFFFF), 0);
                lv_obj_center(abbr_lbl);

                // App name label
                lv_obj_t* name_lbl = lv_label_create(cell);
                lv_label_set_text(name_lbl, s_apps[idx].name);
                lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_CLIP);
                lv_obj_set_width(name_lbl, CELL_W - 4);
                lv_obj_align(name_lbl, LV_ALIGN_BOTTOM_MID, 0, -2);
                lv_obj_set_style_text_color(name_lbl, lv_color_hex(C_ICON_TXT), 0);

                // Touch callback
                lv_obj_add_event_cb(cell, [](lv_event_t* e) {
                    int* pidx = (int*)lv_event_get_user_data(e);
                    if (*pidx < s_napp)
                        purr_wm_launch(s_apps[*pidx].path);
                }, LV_EVENT_CLICKED, new int(idx));
            } else {
                lv_obj_set_style_bg_color(cell, lv_color_hex(C_ICON_BG), 0);
            }
        }
    }
}

// ── Tab switch ────────────────────────────────────────────────────────────
static void tab_switch(int tab) {
    s_tab = tab;
    for (int i = 0; i < TAB_COUNT; i++) {
        if (!s_tab_btns[i]) continue;
        lv_obj_t* lbl = lv_obj_get_child(s_tab_btns[i], 0);
        if (lbl) lv_obj_set_style_text_color(lbl,
            lv_color_hex(i == tab ? C_TAB_ACTIVE : C_TAB_INACT), 0);
    }
    grid_populate();
}

// ── Drawer toggle ─────────────────────────────────────────────────────────
static void open_drawer() {
    if (s_drawer_open || !s_drawer) return;
    s_drawer_open = true;
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_y((lv_obj_t*)obj, v);
    });
    lv_anim_set_var(&a, s_drawer);
    lv_anim_set_values(&a, SCR_H, 0);
    lv_anim_set_time(&a, 250);
    lv_anim_start(&a);
}

static void close_drawer() {
    if (!s_drawer_open || !s_drawer) return;
    s_drawer_open = false;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_y((lv_obj_t*)obj, v);
    });
    lv_anim_set_var(&a, s_drawer);
    lv_anim_set_values(&a, 0, SCR_H);
    lv_anim_set_time(&a, 200);
    lv_anim_set_deleted_cb(&a, nullptr);
    lv_anim_start(&a);
    lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
}

// ── Build home screen ─────────────────────────────────────────────────────
static void build_home(lv_obj_t* parent) {
    s_home_screen = parent;
    lv_obj_set_style_bg_color(parent, lv_color_hex(C_WALL_BG), 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    // Status bar
    lv_obj_t* status = lv_obj_create(parent);
    lv_obj_set_size(status, SCR_W, STATUS_H);
    lv_obj_set_pos(status, 0, 0);
    lv_obj_set_style_bg_color(status, lv_color_hex(C_STATUS_BG), 0);
    lv_obj_set_style_border_width(status, 0, 0);
    lv_obj_set_style_radius(status, 0, 0);
    lv_obj_set_style_pad_all(status, 0, 0);

    s_status_lbl = lv_label_create(status);
    lv_label_set_text(s_status_lbl, kitt.os_name());
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(C_STATUS_TXT), 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    // Time zone
    lv_obj_t* timebar = lv_obj_create(parent);
    lv_obj_set_size(timebar, SCR_W, TIME_H);
    lv_obj_set_pos(timebar, 0, STATUS_H);
    lv_obj_set_style_bg_color(timebar, lv_color_hex(C_TIME_BG), 0);
    lv_obj_set_style_border_width(timebar, 0, 0);
    lv_obj_set_style_radius(timebar, 0, 0);
    lv_obj_set_style_pad_all(timebar, 0, 0);

    s_time_lbl = lv_label_create(timebar);
    lv_label_set_text(s_time_lbl, "00:00");
    lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(C_TIME_TXT), 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t* dev_lbl = lv_label_create(timebar);
    lv_label_set_text(dev_lbl, kitt.device_name());
    lv_obj_set_style_text_color(dev_lbl, lv_color_hex(C_TIME_SUB), 0);
    lv_obj_align(dev_lbl, LV_ALIGN_CENTER, 0, 8);

    // Notification bar
    lv_obj_t* notif = lv_obj_create(parent);
    lv_obj_set_size(notif, SCR_W, NOTIF_H);
    lv_obj_set_pos(notif, 0, STATUS_H + TIME_H);
    lv_obj_set_style_bg_color(notif, lv_color_hex(C_NOTIF_BG), 0);
    lv_obj_set_style_border_width(notif, 0, 0);
    lv_obj_set_style_radius(notif, 0, 0);
    lv_obj_set_style_pad_all(notif, 2, 0);

    lv_obj_t* notif_lbl = lv_label_create(notif);
    lv_label_set_text(notif_lbl, "spkr  MSGS  LOGS  [?]");
    lv_obj_set_style_text_color(notif_lbl, lv_color_hex(C_NOTIF_TXT), 0);
    lv_obj_align(notif_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    // Wallpaper / content
    lv_obj_t* wall = lv_obj_create(parent);
    lv_obj_set_size(wall, SCR_W, CONTENT_H);
    lv_obj_set_pos(wall, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(wall, lv_color_hex(C_WALL_BG), 0);
    lv_obj_set_style_border_width(wall, 0, 0);
    lv_obj_set_style_radius(wall, 0, 0);
    lv_obj_set_style_pad_all(wall, 0, 0);

    // Logo plate
    lv_obj_t* plate = lv_obj_create(wall);
    lv_obj_set_size(plate, 160, 60);
    lv_obj_align(plate, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(plate, lv_color_hex(C_WALL_PLATE), 0);
    lv_obj_set_style_border_width(plate, 0, 0);
    lv_obj_set_style_radius(plate, 4, 0);

    lv_obj_t* logo = lv_label_create(plate);
    lv_label_set_text(logo, "PURR OS");
    lv_obj_set_style_text_color(logo, lv_color_hex(C_WATERMARK), 0);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t* ver = lv_label_create(plate);
    lv_label_set_text(ver, "v" PURR_OS_VERSION);
    lv_obj_set_style_text_color(ver, lv_color_hex(C_TIME_SUB), 0);
    lv_obj_align(ver, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t* hint = lv_label_create(wall);
    lv_label_set_text(hint, "swipe up for apps");
    lv_obj_set_style_text_color(hint, lv_color_hex(C_HINT_TXT), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    // Swipe gesture on wallpaper → open drawer
    lv_obj_add_event_cb(wall, [](lv_event_t* e) {
        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);
        if (vect.y < -30) open_drawer();
    }, LV_EVENT_GESTURE, nullptr);

    // Tab strip
    int tab_w = SCR_W / TAB_COUNT;
    lv_obj_t* tabs = lv_obj_create(parent);
    lv_obj_set_size(tabs, SCR_W, TAB_H);
    lv_obj_set_pos(tabs, 0, TAB_Y);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(C_TAB_BG), 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_set_style_radius(tabs, 0, 0);
    lv_obj_set_style_pad_all(tabs, 0, 0);

    for (int i = 0; i < TAB_COUNT; i++) {
        s_tab_btns[i] = lv_obj_create(tabs);
        lv_obj_set_size(s_tab_btns[i], tab_w, TAB_H);
        lv_obj_set_pos(s_tab_btns[i], i * tab_w, 0);
        lv_obj_set_style_bg_color(s_tab_btns[i], lv_color_hex(C_TAB_BG), 0);
        lv_obj_set_style_border_width(s_tab_btns[i], 0, 0);
        lv_obj_set_style_radius(s_tab_btns[i], 0, 0);
        lv_obj_set_style_pad_all(s_tab_btns[i], 0, 0);

        lv_obj_t* lbl = lv_label_create(s_tab_btns[i]);
        lv_label_set_text(lbl, TAB_NAMES[i]);
        lv_obj_set_style_text_color(lbl,
            lv_color_hex(i == s_tab ? C_TAB_ACTIVE : C_TAB_INACT), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(s_tab_btns[i], [](lv_event_t* e) {
            int* pi = (int*)lv_event_get_user_data(e);
            tab_switch(*pi);
        }, LV_EVENT_CLICKED, new int(i));
    }

    // Dock
    lv_obj_t* dock = lv_obj_create(parent);
    lv_obj_set_size(dock, SCR_W, DOCK_H);
    lv_obj_set_pos(dock, 0, DOCK_Y);
    lv_obj_set_style_bg_color(dock, lv_color_hex(C_DOCK_BG), 0);
    lv_obj_set_style_border_width(dock, 0, 0);
    lv_obj_set_style_radius(dock, 0, 0);
    lv_obj_set_style_pad_all(dock, 0, 0);

    for (int i = 0; i < DOCK_COLS; i++) {
        lv_obj_t* btn = lv_obj_create(dock);
        lv_obj_set_size(btn, DOCK_SW - 4, DOCK_H - 4);
        lv_obj_set_pos(btn, i * DOCK_SW + 2, 2);
        lv_obj_set_style_bg_color(btn, lv_color_hex(DOCK_COLORS[i]), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 3, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, DOCK_LABELS[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int* pi = (int*)lv_event_get_user_data(e);
            Serial.printf("[bbui] dock[%d] tapped\n", *pi);
        }, LV_EVENT_CLICKED, new int(i));
    }

    // Drawer (hidden, slides up on swipe)
    s_drawer = lv_obj_create(parent);
    lv_obj_set_size(s_drawer, SCR_W, SCR_H);
    lv_obj_set_pos(s_drawer, 0, SCR_H);
    lv_obj_set_style_bg_color(s_drawer, lv_color_hex(C_DRAWER_BG), 0);
    lv_obj_set_style_border_width(s_drawer, 0, 0);
    lv_obj_set_style_radius(s_drawer, 0, 0);
    lv_obj_set_style_pad_all(s_drawer, 0, 0);
    lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);

    // Notif bar in drawer
    lv_obj_t* dr_notif = lv_obj_create(s_drawer);
    lv_obj_set_size(dr_notif, SCR_W, NOTIF_H);
    lv_obj_set_pos(dr_notif, 0, 0);
    lv_obj_set_style_bg_color(dr_notif, lv_color_hex(C_NOTIF_BG), 0);
    lv_obj_set_style_border_width(dr_notif, 0, 0);
    lv_obj_set_style_radius(dr_notif, 0, 0);
    lv_obj_set_style_pad_all(dr_notif, 0, 0);

    lv_obj_t* dr_notif_lbl = lv_label_create(dr_notif);
    lv_label_set_text(dr_notif_lbl, "spkr  MSGS  LOGS");
    lv_obj_set_style_text_color(dr_notif_lbl, lv_color_hex(C_NOTIF_TXT), 0);
    lv_obj_align(dr_notif_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    // Drawer tab strip
    lv_obj_t* dr_tabs = lv_obj_create(s_drawer);
    lv_obj_set_size(dr_tabs, SCR_W, TAB_H);
    lv_obj_set_pos(dr_tabs, 0, NOTIF_H);
    lv_obj_set_style_bg_color(dr_tabs, lv_color_hex(C_TAB_BG), 0);
    lv_obj_set_style_border_width(dr_tabs, 0, 0);
    lv_obj_set_style_radius(dr_tabs, 0, 0);
    lv_obj_set_style_pad_all(dr_tabs, 0, 0);

    for (int i = 0; i < TAB_COUNT; i++) {
        lv_obj_t* tb = lv_obj_create(dr_tabs);
        lv_obj_set_size(tb, tab_w, TAB_H);
        lv_obj_set_pos(tb, i * tab_w, 0);
        lv_obj_set_style_bg_color(tb, lv_color_hex(C_TAB_BG), 0);
        lv_obj_set_style_border_width(tb, 0, 0);
        lv_obj_set_style_radius(tb, 0, 0);
        lv_obj_set_style_pad_all(tb, 0, 0);

        lv_obj_t* lbl = lv_label_create(tb);
        lv_label_set_text(lbl, TAB_NAMES[i]);
        lv_obj_set_style_text_color(lbl,
            lv_color_hex(i == s_tab ? C_TAB_ACTIVE : C_TAB_INACT), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(tb, [](lv_event_t* e) {
            int* pi = (int*)lv_event_get_user_data(e);
            tab_switch(*pi);
        }, LV_EVENT_CLICKED, new int(i));
    }

    // App grid container
    int grid_y = NOTIF_H + TAB_H;
    int grid_h = SCR_H - grid_y;
    s_grid_cont = lv_obj_create(s_drawer);
    lv_obj_set_size(s_grid_cont, SCR_W, grid_h);
    lv_obj_set_pos(s_grid_cont, 0, grid_y);
    lv_obj_set_style_bg_color(s_grid_cont, lv_color_hex(C_DRAWER_BG), 0);
    lv_obj_set_style_border_width(s_grid_cont, 0, 0);
    lv_obj_set_style_radius(s_grid_cont, 0, 0);
    lv_obj_set_style_pad_all(s_grid_cont, 0, 0);
    lv_obj_clear_flag(s_grid_cont, LV_OBJ_FLAG_SCROLLABLE);

    grid_populate();

    // Swipe down in drawer → close
    lv_obj_add_event_cb(s_drawer, [](lv_event_t* e) {
        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);
        if (vect.y > 30) close_drawer();
    }, LV_EVENT_GESTURE, nullptr);
}

// ── Shell create function (registered with WM) ─────────────────────────────
static lv_obj_t* blackberry_shell_create(lv_obj_t* parent) {
    scan_apps();
    Serial.printf("[bbui] creating shell  apps=%d\n", s_napp);
    build_home(parent);
    return parent;
}

// ── Update task (time, heartbeat) ──────────────────────────────────────────
static void bbui_update_task(void*) {
#ifdef PURR_BBUI_TARGET_TOUCH
    led_init();
#endif
    for (;;) {
        // Update clock label
        if (s_time_lbl) {
            uint32_t sec = millis() / 1000;
            char tbuf[10];
            snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu",
                     (unsigned long)(sec / 60), (unsigned long)(sec % 60));
            lv_label_set_text(s_time_lbl, tbuf);
        }
#ifdef PURR_BBUI_TARGET_TOUCH
        led_heartbeat();
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ── Public entry point ─────────────────────────────────────────────────────
extern "C" {
void blackberry_ui_start() {
    purr_wm_register_shell(PURR_SHELL_BLACKBERRY, blackberry_shell_create);
    xTaskCreatePinnedToCore(bbui_update_task, "bbui_tick", 4096, nullptr, 1, nullptr, 1);
    Serial.println("[bbui] shell registered");
}
}

#endif  // PURR_HAS_LVGL
#endif  // PURR_HAS_BLACKBERRY_UI
