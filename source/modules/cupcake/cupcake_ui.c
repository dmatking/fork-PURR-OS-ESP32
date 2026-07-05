// cupcake_ui.c — Android 1.5 ("Cupcake")-style launcher: a wallpaper-only
// home screen with a bottom dock (favorites + All Apps), and a full-screen
// all-apps drawer opened from the dock's center button. No pinned icon grid
// on the home screen itself — that's deliberate, so a wallpaper stays fully
// visible instead of being covered by shortcuts.
//
// Status bar + drag-down notification panel below is forked near-verbatim
// from cardstack_ui.c (renamed with a ck_/CK_ prefix) — confirmed while
// planning this that it depends only on LVGL, lv_layer_top(), and generic
// purr_kernel_*() accessors, nothing card-stack-specific. Not ported: the
// home card's baked "PURR OS DP1" strip — this launcher's own home screen
// doesn't need a redundant one.

#include "cupcake.h"
#include "../../kernel/core/purr_kernel.h"
#include "../../kernel/catcalls/purr_win.h"
#include "../app_manager/app_manager.h"
#include "../../assets/icons/blackpurr_icons.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cupcake_ui";

// CUPCAKE_STATUS_PEEK_H is defined in cupcake.h — cupcake_win.c needs it too,
// to keep app windows (and their close buttons) clear of the status hotzone.
#define CUPCAKE_STATUS_EXPANDED_H 220
#define MAX_DOCK_FAV      4
#define MAX_DRAWER_TILES  64
#define DOCK_H            56
// Sized for exactly 2 columns x 2 visible rows on a 320x(240-22) screen —
// drawer width 320 / 2 cols with SPACE_EVENLY gaps, and page_h (~186,
// h - 32px title bar) / 2 rows likewise — the rest of the app list scrolls
// vertically below the first 2x2 page instead of horizontally paging.
#define TILE_W            136
#define TILE_H            80

// All bundled icon assets are 48x48 source images — these are the actual
// on-screen sizes we want, applied via lv_img_set_zoom() (256 = 100%),
// since the container size alone doesn't rescale an lv_img's source bitmap.
#define ICON_PX_TILE 28
#define ICON_PX_DOCK 24
#define ICON_PX_DOCK_CENTER 28
#define ICON_ZOOM(px) (uint16_t)(((px) * 256) / 48)
#define DOCK_FAV_BTN_SIZE    36
#define DOCK_CENTER_BTN_SIZE 44

// ── Icon lookup ──────────────────────────────────────────────────────────────

static const lv_img_dsc_t *icon_for_app(const char *name)
{
    static const struct { const char *app_name; const lv_img_dsc_t *icon; } map[] = {
        { "settings",   &bp_icon_settings_48   },
        { "about",      &bp_icon_about_48      },
        { "terminal",   &bp_icon_terminal_48   },
        { "fileman",    &bp_icon_fileman_48    },
        { "calculator", &bp_icon_calculator_48 },
        { "hwtest",     &bp_icon_tools_48      },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(name, map[i].app_name) == 0) return map[i].icon;
    }
    return &bp_icon_tools_48; // fallback for anything unmatched
}

// Deterministic per-app tint — same technique as cardstack_ui.c's
// app_tint_color(), renamed for this fork.
static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u; // FNV-1a
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

static lv_color_t cupcake_tint_color(const char *name, uint8_t base)
{
    uint32_t h = hash_str(name);
    uint8_t r = (uint8_t)(base + ((h >> 0)  & 0x1F));
    uint8_t g = (uint8_t)(base + ((h >> 8)  & 0x1F));
    uint8_t b = (uint8_t)(base + ((h >> 16) & 0x1F));
    return lv_color_make(r, g, b);
}

// ── Tile builders ────────────────────────────────────────────────────────────

// If the app is already running (and has a tracked window — see
// app_manager.c's window-created hook), tapping its icon again should
// restore that window instead of doing nothing: app_manager_launch_idx()
// on an already-RUNNING app just no-ops (see its re-launch guard), so
// without this an already-open-but-minimized app's icon would appear dead.
static void launch_or_restore(int idx)
{
    const app_entry_t *app = app_manager_get(idx);
    if (app && app->state == APP_STATE_RUNNING && app->window) {
        purr_win_show(app->window);
    } else {
        app_manager_launch_idx(idx);
    }
}

static void home_tile_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "launching app idx=%d", idx);
    launch_or_restore(idx);
}

static lv_obj_t *s_drawer;

static void drawer_tile_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "launching app idx=%d (from drawer)", idx);
    if (s_drawer) lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    launch_or_restore(idx);
}

static void drawer_close_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_drawer) lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
}

static void dock_center_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_drawer) {
        lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_drawer);
    }
}

// Icon + name label, used by the drawer's scrollable grid.
static void build_app_tile(lv_obj_t *parent, int app_idx, const char *name, lv_event_cb_t click_cb)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, TILE_W, TILE_H);
    lv_obj_set_style_radius(tile, 10, 0);
    lv_obj_set_style_bg_color(tile, cupcake_tint_color(name, 0x18), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_60, 0); // lets a wallpaper show through behind icons
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)app_idx);

    lv_obj_t *icon = lv_img_create(tile);
    lv_img_set_src(icon, icon_for_app(name));
    lv_img_set_zoom(icon, ICON_ZOOM(ICON_PX_TILE));
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(tile);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, TILE_W);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
}

// Icon-only round button, used by the dock (favorites + the center
// all-apps launcher) — too short for a name label underneath.
static lv_obj_t *build_icon_button(lv_obj_t *parent, int app_idx, const lv_img_dsc_t *icon,
                                    lv_event_cb_t click_cb, lv_color_t bg,
                                    lv_coord_t size, lv_coord_t icon_px)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, (lv_coord_t)(size / 2), 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0); // lets a wallpaper show through behind icons
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)app_idx);

    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, icon);
    lv_img_set_zoom(img, ICON_ZOOM(icon_px));
    lv_obj_center(img);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return btn;
}

// ── Wallpaper ────────────────────────────────────────────────────────────────
// "default" (the gradient below) or an SD path chosen in Settings, persisted
// in NVS under the same "purr_settings" namespace settings.c already uses.
// Raw RGB565 files only — no on-device image decoder, matching how the
// icon-generation pipeline keeps decoding on the PC side. Falls back to the
// gradient on any missing/malformed file rather than failing to boot.

#define WALLPAPER_PATH_LEN 128

static lv_img_dsc_t s_wallpaper_img;

static bool load_wallpaper_from_sd(const char *path, lv_img_dsc_t *out, uint16_t w, uint16_t h)
{
    size_t expect = (size_t)w * (size_t)h * 2;
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t *buf = heap_caps_malloc(expect, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = heap_caps_malloc(expect, MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); return false; }

    size_t n = fread(buf, 1, expect, f);
    fclose(f);
    if (n != expect) { heap_caps_free(buf); return false; }

    memset(out, 0, sizeof(*out));
    out->header.cf = LV_IMG_CF_TRUE_COLOR;
    out->header.w  = w;
    out->header.h  = h;
    out->data_size = expect;
    out->data      = buf;
    return true;
}

static bool load_wallpaper_choice(lv_img_dsc_t *out, uint16_t w, uint16_t h)
{
    char path[WALLPAPER_PATH_LEN] = "default";
    nvs_handle_t hnd;
    if (nvs_open("purr_settings", NVS_READONLY, &hnd) == ESP_OK) {
        size_t len = sizeof(path);
        nvs_get_str(hnd, "wallpaper", path, &len);
        nvs_close(hnd);
    }
    if (strcmp(path, "default") == 0) return false;
    return load_wallpaper_from_sd(path, out, w, h);
}

// ── Home screen + dock ───────────────────────────────────────────────────────

static lv_obj_t *s_home_screen;
static lv_obj_t *s_dock;

static void build_dock(lv_obj_t *parent, uint16_t w)
{
    s_dock = lv_obj_create(parent);
    lv_obj_remove_style_all(s_dock);
    lv_obj_set_size(s_dock, w, DOCK_H);
    lv_obj_align(s_dock, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_dock, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dock, LV_OPA_50, 0);
    lv_obj_clear_flag(s_dock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(s_dock, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dock, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Favorites grouped together on the left, All Apps button on its own at
    // the right — "first N apps in registry order" (no separate favorites
    // concept yet). The dock is the only place shortcuts live now — the home
    // screen itself stays clear so a wallpaper isn't covered by an icon grid.
    int n = app_manager_count();
    int fav_n = (n < MAX_DOCK_FAV) ? n : MAX_DOCK_FAV;

    for (int i = 0; i < fav_n; i++) {
        const app_entry_t *app = app_manager_get(i);
        if (!app) continue;
        build_icon_button(s_dock, i, icon_for_app(app->name), home_tile_click_cb,
                           cupcake_tint_color(app->name, 0x18), DOCK_FAV_BTN_SIZE, ICON_PX_DOCK);
    }

    build_icon_button(s_dock, -1, BP_ICON_ALL_APPS_48, dock_center_click_cb,
                       lv_color_make(0x40, 0x40, 0x40), DOCK_CENTER_BTN_SIZE, ICON_PX_DOCK_CENTER);
}

static void build_home_screen(uint16_t w, uint16_t h)
{
    s_home_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_home_screen);
    lv_obj_set_size(s_home_screen, w, h);
    lv_obj_set_pos(s_home_screen, 0, CUPCAKE_STATUS_PEEK_H);
    lv_obj_clear_flag(s_home_screen, LV_OBJ_FLAG_SCROLLABLE);

    if (load_wallpaper_choice(&s_wallpaper_img, w, h)) {
        lv_obj_t *bg = lv_img_create(s_home_screen);
        lv_img_set_src(bg, &s_wallpaper_img);
        lv_obj_set_pos(bg, 0, 0);
        lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_set_style_bg_color(s_home_screen, lv_color_make(0x10, 0x18, 0x30), 0);
        lv_obj_set_style_bg_grad_color(s_home_screen, lv_color_make(0x05, 0x08, 0x14), 0);
        lv_obj_set_style_bg_grad_dir(s_home_screen, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(s_home_screen, LV_OPA_COVER, 0);
    }

    // No pinned icon grid on the home screen — shortcuts live in the dock
    // only now, so a wallpaper stays fully visible above it.
    build_dock(s_home_screen, w);
}

// ── App drawer (full-screen overlay) ────────────────────────────────────────

static lv_obj_t *s_drawer_grid;

static void build_drawer(uint16_t w, uint16_t h)
{
    s_drawer = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_drawer);
    lv_obj_set_size(s_drawer, w, h);
    lv_obj_set_pos(s_drawer, 0, CUPCAKE_STATUS_PEEK_H);
    lv_obj_set_style_bg_color(s_drawer, lv_color_make(0x18, 0x18, 0x18), 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title_bar = lv_obj_create(s_drawer);
    lv_obj_remove_style_all(title_bar);
    lv_obj_set_size(title_bar, w, 32);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, "All Apps");
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t *close_btn = lv_label_create(title_bar);
    lv_obj_set_style_text_color(close_btn, lv_color_white(), 0);
    lv_label_set_text(close_btn, LV_SYMBOL_CLOSE);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close_btn, drawer_close_click_cb, LV_EVENT_CLICKED, NULL);

    // Fixed 2-column grid that scrolls vertically for anything past the
    // first 2x2 page, instead of horizontally paging in groups of 6 — tiles
    // are built directly into s_drawer_grid, no per-page wrapper container.
    lv_coord_t page_h = (lv_coord_t)(h - 32);

    s_drawer_grid = lv_obj_create(s_drawer);
    lv_obj_remove_style_all(s_drawer_grid);
    lv_obj_set_size(s_drawer_grid, w, page_h);
    lv_obj_set_pos(s_drawer_grid, 0, 32);
    lv_obj_set_style_bg_opa(s_drawer_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(s_drawer_grid, LV_DIR_VER);
    lv_obj_clear_flag(s_drawer_grid, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_layout(s_drawer_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_drawer_grid, LV_FLEX_FLOW_ROW_WRAP);
    // SPACE_EVENLY's gap comes from *leftover* space in the container — with
    // more than 4 apps (7 here: settings/terminal/fileman/calculator/hwtest/
    // drivermgr/meshchat) the grid overflows page_h and there's no leftover
    // to distribute, so the row gap collapsed to 0 and rows rendered flush
    // against each other, looking like overlapping tiles. Fixed pad_row/
    // pad_column gaps hold regardless of overflow; START packs rows from
    // the top instead of trying to spread them across the (insufficient) space.
    lv_obj_set_flex_align(s_drawer_grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_drawer_grid, 8, 0);
    lv_obj_set_style_pad_row(s_drawer_grid, 10, 0);
    lv_obj_set_style_pad_column(s_drawer_grid, 10, 0);

    int n = app_manager_count();
    if (n > MAX_DRAWER_TILES) n = MAX_DRAWER_TILES;

    for (int i = 0; i < n; i++) {
        const app_entry_t *app = app_manager_get(i);
        if (!app) continue;
        build_app_tile(s_drawer_grid, i, app->name, drawer_tile_click_cb);
    }
}

// ── Status panels (forked from cardstack_ui.c, ck_/CK_ prefix) ──────────────
// Two independent drag-down panels instead of one: which half of the screen
// the swipe starts from decides which one opens — left = Notifications,
// right = Running Apps (labeled that way for now rather than "Quick
// Settings" since there are no real toggles to put there yet; a natural
// place to add them later).

typedef enum { CK_STATUS_PEEK, CK_STATUS_EXPANDED } ck_status_state_t;

typedef struct {
    lv_obj_t          *panel;
    lv_obj_t          *handle;   // grab handle, shown only once expanded
    ck_status_state_t  state;
    lv_coord_t         press_y0;
    lv_coord_t         base_y;
} ck_panel_t;

static ck_panel_t s_notif_panel;
static ck_panel_t s_quick_panel;

static lv_obj_t *s_status_hotzone_left;
static lv_obj_t *s_status_hotzone_right;
static lv_obj_t *s_icon_wifi;
static lv_obj_t *s_icon_lora;
static lv_obj_t *s_icon_mail;
static lv_obj_t *s_icon_mail_badge;
static lv_obj_t *s_icon_battery;
static lv_obj_t *s_status_notif_box;
static lv_obj_t *s_status_taskmgr_box;

static lv_coord_t ck_panel_y_for_state(ck_status_state_t s)
{
    switch (s) {
        case CK_STATUS_PEEK:     return (lv_coord_t)(-(CUPCAKE_STATUS_EXPANDED_H - CUPCAKE_STATUS_PEEK_H));
        case CK_STATUS_EXPANDED: return 0;
    }
    return (lv_coord_t)(-(CUPCAKE_STATUS_EXPANDED_H - CUPCAKE_STATUS_PEEK_H));
}

static void ck_panel_set_state(ck_panel_t *p, ck_status_state_t s)
{
    p->state = s;
    lv_obj_set_y(p->panel, ck_panel_y_for_state(s));
    if (s == CK_STATUS_EXPANDED) lv_obj_clear_flag(p->handle, LV_OBJ_FLAG_HIDDEN);
    else                          lv_obj_add_flag(p->handle, LV_OBJ_FLAG_HIDDEN);
}

// Shared drag handler for both panels and both hotzones — which panel it's
// dragging is passed as user_data (&s_notif_panel or &s_quick_panel).
static void ck_panel_drag_event_cb(lv_event_t *e)
{
    ck_panel_t *p = (ck_panel_t *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    if (code == LV_EVENT_PRESSED) {
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        p->press_y0 = pt.y;
        p->base_y   = lv_obj_get_y(p->panel);
    } else if (code == LV_EVENT_PRESSING) {
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        lv_coord_t dy = (lv_coord_t)(pt.y - p->press_y0);
        lv_coord_t ny = (lv_coord_t)(p->base_y + dy);
        if (ny < ck_panel_y_for_state(CK_STATUS_PEEK)) ny = ck_panel_y_for_state(CK_STATUS_PEEK);
        if (ny > 0) ny = 0;
        lv_obj_set_y(p->panel, ny);
    } else if (code == LV_EVENT_RELEASED) {
        lv_coord_t y = lv_obj_get_y(p->panel);
        ck_status_state_t target = (y > -(CUPCAKE_STATUS_EXPANDED_H / 2)) ? CK_STATUS_EXPANDED : CK_STATUS_PEEK;
        ck_panel_set_state(p, target);
    }
}

// Both panels are full width once expanded, so only one should be open at a
// time — starting a drag on one collapses the other first.
static void ck_hotzone_left_pressed_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    if (s_quick_panel.state == CK_STATUS_EXPANDED) ck_panel_set_state(&s_quick_panel, CK_STATUS_PEEK);
}
static void ck_hotzone_right_pressed_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    if (s_notif_panel.state == CK_STATUS_EXPANDED) ck_panel_set_state(&s_notif_panel, CK_STATUS_PEEK);
}

static void ck_build_panel(ck_panel_t *p, uint16_t w, const char *title, lv_obj_t **out_box)
{
    p->panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(p->panel);
    lv_obj_set_size(p->panel, w, CUPCAKE_STATUS_EXPANDED_H);
    lv_obj_set_pos(p->panel, 0, ck_panel_y_for_state(CK_STATUS_PEEK));
    lv_obj_set_style_bg_color(p->panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(p->panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(p->panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p->panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p->panel, ck_panel_drag_event_cb, LV_EVENT_PRESSED, p);
    lv_obj_add_event_cb(p->panel, ck_panel_drag_event_cb, LV_EVENT_PRESSING, p);
    lv_obj_add_event_cb(p->panel, ck_panel_drag_event_cb, LV_EVENT_RELEASED, p);

    lv_obj_t *title_lbl = lv_label_create(p->panel);
    lv_obj_set_style_text_color(title_lbl, lv_color_make(0xA0, 0xA0, 0xA0), 0);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_pos(title_lbl, 4, CUPCAKE_STATUS_PEEK_H + 2);

    p->handle = lv_obj_create(p->panel);
    lv_obj_remove_style_all(p->handle);
    lv_obj_set_size(p->handle, 40, 5);
    lv_obj_set_style_bg_color(p->handle, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_set_style_bg_opa(p->handle, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p->handle, 3, 0);
    lv_obj_clear_flag(p->handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(p->handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(p->handle, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_flag(p->handle, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *box = lv_obj_create(p->panel);
    lv_obj_remove_style_all(box);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_size(box, (lv_coord_t)(w - 8), 160);
    lv_obj_set_pos(box, 4, CUPCAKE_STATUS_PEEK_H + 18);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);

    *out_box = box;
}

// Forward-declared: defined further down alongside the rest of the
// notifications/task-manager refresh logic, but needed here to wire up the
// panels' title-bar action buttons at build time.
static void ck_notif_clear_cb(lv_event_t *e);
static void ck_taskmgr_home_cb(lv_event_t *e);

static void ck_build_status_panels(uint16_t w)
{
    ck_build_panel(&s_notif_panel, w, "Notifications", &s_status_notif_box);
    ck_build_panel(&s_quick_panel, w, "Running Apps", &s_status_taskmgr_box);

    // Action button on the empty right side of each panel's title bar,
    // opposite the title label (which sits at x=4) — same clickable-label
    // style already used for the drawer's title-bar close icon.
    lv_obj_t *notif_clear_btn = lv_label_create(s_notif_panel.panel);
    lv_obj_set_style_text_color(notif_clear_btn, lv_color_white(), 0);
    lv_label_set_text(notif_clear_btn, LV_SYMBOL_CLOSE);
    lv_obj_set_pos(notif_clear_btn, (lv_coord_t)(w - 20), CUPCAKE_STATUS_PEEK_H + 2);
    lv_obj_add_flag(notif_clear_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(notif_clear_btn, ck_notif_clear_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *taskmgr_home_btn = lv_label_create(s_quick_panel.panel);
    lv_obj_set_style_text_color(taskmgr_home_btn, lv_color_white(), 0);
    lv_label_set_text(taskmgr_home_btn, LV_SYMBOL_HOME);
    lv_obj_set_pos(taskmgr_home_btn, (lv_coord_t)(w - 20), CUPCAKE_STATUS_PEEK_H + 2);
    lv_obj_add_flag(taskmgr_home_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(taskmgr_home_btn, ck_taskmgr_home_cb, LV_EVENT_CLICKED, NULL);

    s_status_hotzone_left = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_status_hotzone_left);
    lv_obj_set_size(s_status_hotzone_left, (lv_coord_t)(w / 2), CUPCAKE_STATUS_PEEK_H);
    lv_obj_set_pos(s_status_hotzone_left, 0, 0);
    lv_obj_set_style_bg_opa(s_status_hotzone_left, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_status_hotzone_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_status_hotzone_left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_status_hotzone_left, ck_hotzone_left_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_status_hotzone_left, ck_panel_drag_event_cb, LV_EVENT_PRESSED, &s_notif_panel);
    lv_obj_add_event_cb(s_status_hotzone_left, ck_panel_drag_event_cb, LV_EVENT_PRESSING, &s_notif_panel);
    lv_obj_add_event_cb(s_status_hotzone_left, ck_panel_drag_event_cb, LV_EVENT_RELEASED, &s_notif_panel);

    s_status_hotzone_right = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_status_hotzone_right);
    lv_obj_set_size(s_status_hotzone_right, (lv_coord_t)(w - w / 2), CUPCAKE_STATUS_PEEK_H);
    lv_obj_set_pos(s_status_hotzone_right, (lv_coord_t)(w / 2), 0);
    lv_obj_set_style_bg_opa(s_status_hotzone_right, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_status_hotzone_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_status_hotzone_right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_status_hotzone_right, ck_hotzone_right_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_status_hotzone_right, ck_panel_drag_event_cb, LV_EVENT_PRESSED, &s_quick_panel);
    lv_obj_add_event_cb(s_status_hotzone_right, ck_panel_drag_event_cb, LV_EVENT_PRESSING, &s_quick_panel);
    lv_obj_add_event_cb(s_status_hotzone_right, ck_panel_drag_event_cb, LV_EVENT_RELEASED, &s_quick_panel);
}

// The envelope/mail icon always opens Notifications specifically, regardless
// of which hotzone half was last used.
static void ck_mail_icon_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_quick_panel.state == CK_STATUS_EXPANDED) ck_panel_set_state(&s_quick_panel, CK_STATUS_PEEK);
    ck_panel_set_state(&s_notif_panel, s_notif_panel.state == CK_STATUS_PEEK ? CK_STATUS_EXPANDED : CK_STATUS_PEEK);
}

#define CK_ICON_ON  lv_color_make(0x4D, 0xD0, 0x6B)
#define CK_ICON_OFF lv_color_make(0x55, 0x55, 0x55)

static void ck_build_status_icons(uint16_t w)
{
    s_icon_battery = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(s_icon_battery, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_icon_battery, lv_color_white(), 0);
    lv_label_set_text(s_icon_battery, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_pos(s_icon_battery, 6, 4);

    s_icon_wifi = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(s_icon_wifi, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_icon_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_pos(s_icon_wifi, (lv_coord_t)(w - 58), 4);

    s_icon_lora = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(s_icon_lora, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_icon_lora, LV_SYMBOL_GPS);
    lv_obj_set_pos(s_icon_lora, (lv_coord_t)(w - 36), 4);

    s_icon_mail = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(s_icon_mail, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_icon_mail, lv_color_white(), 0);
    lv_label_set_text(s_icon_mail, LV_SYMBOL_ENVELOPE);
    lv_obj_set_pos(s_icon_mail, (lv_coord_t)(w - 16), 4);
    lv_obj_add_flag(s_icon_mail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_icon_mail, ck_mail_icon_click_cb, LV_EVENT_CLICKED, NULL);

    s_icon_mail_badge = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_icon_mail_badge);
    lv_obj_set_size(s_icon_mail_badge, 6, 6);
    lv_obj_set_style_bg_color(s_icon_mail_badge, lv_color_make(0xE0, 0x30, 0x30), 0);
    lv_obj_set_style_bg_opa(s_icon_mail_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_icon_mail_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(s_icon_mail_badge, (lv_coord_t)(w - 8), 2);
    lv_obj_clear_flag(s_icon_mail_badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_icon_mail_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_icon_mail_badge, LV_OBJ_FLAG_HIDDEN);
}

static void ck_refresh_status_icons(void)
{
    lv_obj_set_style_text_color(s_icon_wifi, purr_kernel_wifi_connected() ? CK_ICON_ON : CK_ICON_OFF, 0);
    lv_obj_set_style_text_color(s_icon_lora, purr_kernel_lora_available() ? CK_ICON_ON : CK_ICON_OFF, 0);

    if (purr_kernel_notify_count() > 0) lv_obj_clear_flag(s_icon_mail_badge, LV_OBJ_FLAG_HIDDEN);
    else                                 lv_obj_add_flag(s_icon_mail_badge, LV_OBJ_FLAG_HIDDEN);

    int pct = purr_kernel_battery_percent();
    const char *sym;
    lv_color_t  color = lv_color_white();
    if (pct < 0)        { sym = LV_SYMBOL_BATTERY_FULL;  color = CK_ICON_OFF; }
    else if (pct > 80)  sym = LV_SYMBOL_BATTERY_FULL;
    else if (pct > 55)  sym = LV_SYMBOL_BATTERY_3;
    else if (pct > 30)  sym = LV_SYMBOL_BATTERY_2;
    else if (pct > 10)  sym = LV_SYMBOL_BATTERY_1;
    else                { sym = LV_SYMBOL_BATTERY_EMPTY; color = lv_color_make(0xE0, 0x40, 0x40); }
    lv_label_set_text(s_icon_battery, sym);
    lv_obj_set_style_text_color(s_icon_battery, color, 0);
}

static void ck_refresh_status_notif_box(void)
{
    lv_obj_clean(s_status_notif_box);
    int n = purr_kernel_notify_count();
    for (int i = 0; i < n; i++) {
        purr_notification_t note;
        if (!purr_kernel_notify_at(i, &note)) break;
        lv_obj_t *row = lv_label_create(s_status_notif_box);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(row, lv_color_white(), 0);
        char line[PURR_NOTIFY_TITLE_LEN + PURR_NOTIFY_BODY_LEN + 4];
        snprintf(line, sizeof(line), "%s: %s", note.title, note.body);
        lv_label_set_text(row, line);
    }
}

static void ck_notif_clear_cb(lv_event_t *e)
{
    (void)e;
    purr_kernel_notify_clear();
    ck_refresh_status_notif_box();
}

// ── Running Apps (task manager, in the drag-down panel) ─────────────────────
// Same data/row pattern as cardstack_ui.c's Task Manager card
// (build_taskmgr_card/refresh_taskmgr_card/taskmgr_kill_cb) — Cupcake has no
// card stack to host an equivalent card in, so this lives in the
// notification panel instead, since that's the one persistent overlay
// already reachable from anywhere.

static void ck_taskmgr_kill_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    app_manager_stop(idx);
}

// Full "return to home screen" — hides the drawer (if open) and every
// currently-visible running app's window, then collapses this panel.
// Apps keep running (this only hides their windows, same as Minimize) — it's
// a navigation shortcut, not a kill-all.
static void ck_taskmgr_home_cb(lv_event_t *e)
{
    (void)e;
    if (s_drawer) lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    int n = app_manager_count();
    for (int i = 0; i < n; i++) {
        const app_entry_t *app = app_manager_get(i);
        if (app && app->state == APP_STATE_RUNNING && app->window) {
            purr_win_hide(app->window);
        }
    }
    ck_panel_set_state(&s_quick_panel, CK_STATUS_PEEK);
}

static void ck_taskmgr_open_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const app_entry_t *app = app_manager_get(idx);
    if (app && app->window) purr_win_show(app->window);
}

static void ck_refresh_status_taskmgr(void)
{
    lv_obj_clean(s_status_taskmgr_box);
    int n = app_manager_count();
    bool any = false;
    for (int i = 0; i < n; i++) {
        const app_entry_t *app = app_manager_get(i);
        if (!app || app->state != APP_STATE_RUNNING) continue;
        any = true;

        lv_obj_t *row = lv_obj_create(s_status_taskmgr_box);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 24);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_label_set_text(lbl, app->name);
        lv_obj_set_pos(lbl, 0, 4);

        lv_obj_t *kill_btn = lv_btn_create(row);
        lv_obj_set_size(kill_btn, 44, 20);
        lv_obj_align(kill_btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t *kill_lbl = lv_label_create(kill_btn);
        lv_label_set_text(kill_lbl, "Kill");
        lv_obj_center(kill_lbl);
        lv_obj_add_event_cb(kill_btn, ck_taskmgr_kill_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *open_btn = lv_btn_create(row);
        lv_obj_set_size(open_btn, 44, 20);
        lv_obj_align(open_btn, LV_ALIGN_RIGHT_MID, -48, 0);
        lv_obj_t *open_lbl = lv_label_create(open_btn);
        lv_label_set_text(open_lbl, "Open");
        lv_obj_center(open_lbl);
        lv_obj_add_event_cb(open_btn, ck_taskmgr_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (!any) {
        lv_obj_t *lbl = lv_label_create(s_status_taskmgr_box);
        lv_obj_set_style_text_color(lbl, lv_color_make(0x80, 0x80, 0x80), 0);
        lv_label_set_text(lbl, "No running apps");
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void cupcake_ui_init(void)
{
    uint16_t w = cupcake_hal_width();
    uint16_t h = cupcake_hal_height();
    uint16_t body_h = (uint16_t)(h - CUPCAKE_STATUS_PEEK_H);

    build_home_screen(w, body_h);
    build_drawer(w, body_h);

    ck_build_status_panels(w);
    ck_build_status_icons(w);

    ESP_LOGI(TAG, "cupcake home screen built (%d total apps)", app_manager_count());
}

void cupcake_ui_tick(void)
{
    ck_refresh_status_notif_box();
    ck_refresh_status_taskmgr();
    ck_refresh_status_icons();
}
