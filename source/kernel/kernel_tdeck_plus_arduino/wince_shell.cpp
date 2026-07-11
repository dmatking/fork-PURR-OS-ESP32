// wince_shell.cpp — baked-in WinCE desktop shell for tdeck_plus_arduino.
//
// Ported from PURR-OS-0.11/devices/tdeck_plus/purr_app.cpp, ground straight
// into the kernel per explicit instruction: no .purr module wrapper, no
// catcall indirection for the UI — this file talks to MiniWin's mw_* API
// directly. The catalog only lists apps that map onto state this kernel
// already exposes via purr_kernel_*(); 0.11 apps needing a Lua VM, the
// MagicMac/MagiDOS emulators, or NVS boot-mode switching aren't ported —
// those subsystems don't exist here.
//
// Wallpaper, desktop buttons, the taskbar+Start Menu, and the lock overlay
// are four SEPARATE, real z-ordered MiniWin windows (not one big window
// manually dispatching touches by coordinate range) — mirrors the same
// split in source/modules/miniwin/miniwin_wince_desktop.c; see that file's
// top comment for the full rationale (MiniWin resolves touch by picking
// the highest z-order window whose window_rect contains the point, so each
// window's rect has to track its real visual footprint).
#include "wince_shell.h"
#include "wince_common.h"
#include "wince_taskbar.h"
#include "wince_apps.h"

#include "miniwin.h"
#include "miniwin_utilities.h"
#include "miniwin_keyboard.h"
#include "miniwin_cursor.h"
#include "miniwin_lock.h"
#include "gl/gl.h"
#include "hal/hal_lcd.h"
#include "hal/hal_non_vol.h"
#include "hal/hal_timer.h"
#include "hal/hal_touch.h"
#include "purr_kernel.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

extern "C" void miniwin_win_register(void);

static const char *TAG = "wince";

#define SCR_W       mw_hal_lcd_get_display_width()
#define SCR_H       mw_hal_lcd_get_display_height()
#define TASKBAR_Y   (SCR_H - TASKBAR_H)
#define START_X     2
#define START_Y     (TASKBAR_Y + 2)
#define START_W     52
#define START_H     (TASKBAR_H - 4)
#define SMENU_W     130
#define SMENU_IH    18
#define SMENU_SEP_H 8
#define SMENU_X     0
#define SMENU_TL_H  (2 * SMENU_IH + SMENU_SEP_H + 4)

// Lock overlay's "tap to unlock" hotspot — a centered button, hit-tested by
// lock_message() against exactly the same rect lock_paint() draws it at.
// The lock window is always full-screen at origin (0,0), so these stay
// screen-absolute == window-local, unlike the taskbar+menu geometry below.
#define LOCK_BTN_W 100
#define LOCK_BTN_H 30

static void lock_hotspot_rect(mw_util_rect_t *out) {
    mw_util_set_rect(out, (int16_t)((SCR_W - LOCK_BTN_W) / 2), (int16_t)(SCR_H / 2 + 16),
                      LOCK_BTN_W, LOCK_BTN_H);
}

// Battery/RAM footer at the bottom of the lock overlay — so battery level
// is visible without unlocking. Refreshed on its own slow timer (see
// LOCK_INFO_REFRESH_MS in wince_shell_task()), independent of the overlay's
// own one-shot repaint-on-transition — that's the only thing that should
// still redraw while locked, and this footer needs to actually stay current
// over a long lock instead of freezing at whatever it read the instant the
// screen locked.
#define LOCK_INFO_H 16

static void lock_info_rect(mw_util_rect_t *out) {
    mw_util_set_rect(out, 0, (int16_t)(SCR_H - LOCK_INFO_H), SCR_W, LOCK_INFO_H);
}

static void draw_lock_info(const mw_gl_draw_info_t *d) {
    mw_util_rect_t r;
    lock_info_rect(&r);
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(0x000000);
    mw_gl_rectangle(d, r.x, r.y, r.width, r.height);

    // Oversized on purpose — GCC's format-truncation check can't prove %d
    // stays short even though these values are always small in practice.
    char line[48];
    int mv = purr_kernel_battery_voltage_mv();
    unsigned free_kb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024);
    if (mv >= 0) {
        snprintf(line, sizeof(line), "Batt:%d.%02uV  RAM:%uKB free",
                 mv / 1000, (unsigned)((mv % 1000) / 10), free_kb);
    } else {
        snprintf(line, sizeof(line), "Batt: --  RAM:%uKB free", free_kb);
    }
    mw_gl_set_fg_colour(0xFFFFFF);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    int16_t lw = mw_gl_get_string_width_pixels(line);
    mw_gl_string(d, (int16_t)((SCR_W - lw) / 2), (int16_t)(r.y + (LOCK_INFO_H - 9) / 2), line);
}

#define DSK_BTN_W   52
#define DSK_BTN_H   52
#define DSK_BTN_X   8
#define DSK_FILES_Y 8
#define DSK_ABOUT_Y (DSK_FILES_Y + DSK_BTN_H + 6)

struct wince_catalog_entry_t {
    const char *name;
    void (*launch)(void);
};

static const wince_catalog_entry_t s_catalog[] = {
    { "About", app_about_launch },
    { "WiFi",  app_wifi_launch  },
    { "LoRa",  app_lora_launch  },
    { "Files", app_files_launch },
};
static const int s_catalog_count = (int)(sizeof(s_catalog) / sizeof(s_catalog[0]));

// Programs folder caps each page to SMENU_PAGE_SIZE items with a trailing
// "More (x/y)" row that cycles pages (wraps back to page 0 past the last)
// — keeps the submenu box from growing past the taskbar/off the top of the
// screen as the catalog grows, instead of the old unbounded-height list.
#define SMENU_PAGE_SIZE 7

typedef struct {
    int  page_start;
    int  page_items;   // items shown on this page (<= SMENU_PAGE_SIZE)
    int  page_count;   // total pages (>= 1)
    bool has_more;      // page_count > 1 — draws/handles the "More" row
    int  max_items;     // 1 (Back) + page_items + (has_more ? 1 : 0)
} smenu_page_t;

// Clamps *page into range as a side effect (list can shrink under a stale
// page index — same defensive clamp s_node_idx uses elsewhere in this
// codebase for a shrinking table).
static smenu_page_t smenu_paginate(int total, int *page) {
    smenu_page_t p;
    p.page_count = (total + SMENU_PAGE_SIZE - 1) / SMENU_PAGE_SIZE;
    if (p.page_count < 1) p.page_count = 1;
    if (*page >= p.page_count) *page = 0;
    p.page_start = *page * SMENU_PAGE_SIZE;
    p.page_items = total - p.page_start;
    if (p.page_items > SMENU_PAGE_SIZE) p.page_items = SMENU_PAGE_SIZE;
    if (p.page_items < 0) p.page_items = 0;
    p.has_more  = p.page_count > 1;
    p.max_items = 1 + p.page_items + (p.has_more ? 1 : 0);
    return p;
}

static void draw_smenu_box(const mw_gl_draw_info_t *d, int16_t smy, int16_t smh)
{
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_MBKG);
    mw_gl_rectangle(d, SMENU_X, smy, SMENU_W, smh);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_set_fill(MW_GL_NO_FILL); mw_gl_set_border(MW_GL_BORDER_ON);
    mw_gl_rectangle(d, SMENU_X, smy, SMENU_W, smh);
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
}

// ── Window 1: Wallpaper (z=1, lowest) ────────────────────────────────────────

static mw_handle_t s_wallpaper_handle = MW_INVALID_HANDLE;

static void wallpaper_paint(mw_handle_t handle, const mw_gl_draw_info_t *d) {
    (void)handle;
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_DESKTOP);
    mw_gl_rectangle(d, 0, 0, (int16_t)SCR_W, (int16_t)SCR_H);
}

// Nothing to do — this window only ever receives a touch that missed every
// window above it (desktop buttons, taskbar+menu, and while locked, lock).
static void wallpaper_message(const mw_message_t *msg) { (void)msg; }

// ── Window 2: Desktop buttons (z=2) ──────────────────────────────────────────
//
// This kernel's desktop only ever has two fixed buttons (Files/About), not
// a full icon grid — same window-split role as miniwin_wince_desktop.c's
// icons window, just simpler content. Rect is full screen minus the
// taskbar strip, same reasoning as that file's icons window.

static mw_handle_t s_dtbtn_handle = MW_INVALID_HANDLE;

static void draw_desktop_btn(const mw_gl_draw_info_t *d,
                              int16_t x, int16_t y, const char *label)
{
    wince_draw_raised(d, x, y, DSK_BTN_W, DSK_BTN_H, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    int16_t lw = mw_gl_get_string_width_pixels(label);
    mw_gl_string(d, (int16_t)(x + (DSK_BTN_W - lw) / 2),
                 (int16_t)(y + (DSK_BTN_H - 9) / 2), label);
}

static void dtbtn_paint(mw_handle_t handle, const mw_gl_draw_info_t *d) {
    (void)handle;
    // Self-sufficient background fill — see icons_paint()'s equivalent
    // comment in miniwin_wince_desktop.c. This window doesn't get to
    // assume the separate, lower z-order wallpaper window already cleared
    // this region; without this fill, closing an app or the lock overlay
    // left stale pixels behind here.
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_DESKTOP);
    mw_gl_rectangle(d, 0, 0, (int16_t)SCR_W, TASKBAR_Y);
    draw_desktop_btn(d, DSK_BTN_X, DSK_FILES_Y, "Files");
    draw_desktop_btn(d, DSK_BTN_X, DSK_ABOUT_Y, "About");
}

static void dtbtn_message(const mw_message_t *msg) {
    if (msg->message_id != MW_TOUCH_DOWN_MESSAGE) return;
    int16_t tx = (int16_t)(msg->message_data >> 16);
    int16_t ty = (int16_t)(msg->message_data & 0xFFFF);
    if (tx >= DSK_BTN_X && tx < DSK_BTN_X + DSK_BTN_W) {
        if (ty >= DSK_FILES_Y && ty < DSK_FILES_Y + DSK_BTN_H) {
            app_files_launch();
            mw_paint_all();
            return;
        }
        if (ty >= DSK_ABOUT_Y && ty < DSK_ABOUT_Y + DSK_BTN_H) {
            app_about_launch();
            mw_paint_all();
            return;
        }
    }
}

// ── Window 3: Taskbar + Start Menu (z=3) ─────────────────────────────────────
//
// Resizes itself live: rect is just the bottom strip when the Start Menu is
// closed, and grows upward to cover the popup the instant it opens — see
// current_menu_height()'s comment for exactly how that turns every old
// screen-absolute Y coordinate here into a window-local one (MiniWin
// delivers both paint and touch coordinates already translated relative to
// a window's own current origin).

static mw_handle_t s_taskbar_handle  = MW_INVALID_HANDLE;
static bool s_smenu_open    = false;
static int  s_smenu_folder  = -1;  // -1 = top level, 0 = programs
static int  s_smenu_sel     = 0;
static int  s_smenu_page    = 0;   // current page within the programs folder
static bool s_smenu_pressed = false;

// Taskbar corner rotates between free RAM and battery voltage every
// STATUS_ROTATE_TICKS repaints — flipped in wince_shell_task() (which
// already repaints this exact rect once a second for RAM freshness),
// read here in taskbar_paint() to decide which string to format.
static bool s_status_show_battery = false;

// Height (px) the Start Menu currently needs above the taskbar strip — see
// miniwin_wince_desktop.c's current_menu_height() for the full explanation
// of why this is also the fixed offset that turns screen-absolute taskbar
// coordinates into window-local ones (add menu_h) while Start Menu box
// coordinates simply drop their old "smy" term (local smy is always 0).
static int16_t current_menu_height(void) {
    if (!s_smenu_open) return 0;
    if (s_smenu_folder < 0) return SMENU_TL_H;
    smenu_page_t pg = smenu_paginate(s_catalog_count, &s_smenu_page);
    return (int16_t)(pg.max_items * SMENU_IH + 4);
}

static void wce_status_rect(mw_util_rect_t *out) {
    int16_t menu_h = current_menu_height();
    mw_util_set_rect(out, (int16_t)(SCR_W - 50), (int16_t)(menu_h + 2), 48, TASKBAR_H - 4);
}

// Explicitly repaints every system layer window by handle — see
// miniwin_wince_desktop.c's wce_redraw_layers() for the full rationale
// (mw_paint_all() walks z-order 0..N assuming no gaps, and
// mw_bring_window_to_front() — called on every lock/unlock and taskbar
// focus switch — creates exactly that kind of gap).
static void redraw_layers(void) {
    mw_paint_window_client(s_wallpaper_handle);
    mw_paint_window_client(s_dtbtn_handle);
    mw_paint_window_client(s_taskbar_handle);
    // Belt and suspenders: the three handle-targeted repaints above rely on
    // MiniWin's per-window occlusion computation (do_paint_window_client())
    // correctly figuring out "what's really on top of me right now" for a
    // window that ISN'T currently focused/frontmost (desktop buttons and
    // wallpaper essentially never are) — confirmed live that alone isn't
    // reliably clearing everything a just-closed/just-unlocked window left
    // behind. mw_paint_all() takes a completely different path (walks
    // z-order low to high, each window's paint straightforwardly
    // overwriting the last rather than pre-computing "don't touch this
    // region"), so it catches whatever the targeted calls above miss.
    mw_paint_all();
}

// Resizes/repositions the taskbar+menu window to exactly match its current
// visual footprint and repaints it. Call after anything that could change
// current_menu_height()'s result (smenu open/close/folder/page).
static void resize_taskbar_window(void) {
    int16_t menu_h = current_menu_height();
    int16_t new_h  = (int16_t)(TASKBAR_H + menu_h);
    int16_t new_y  = (int16_t)(SCR_H - new_h);
    mw_reposition_window(s_taskbar_handle, 0, new_y);
    mw_resize_window(s_taskbar_handle, (int16_t)SCR_W, new_h);
    // The Start Menu can vacate screen area the desktop-buttons window
    // owns when it shrinks back down — that window doesn't get an
    // automatic repaint just because the window above it moved, so this
    // needs the full redraw_layers() treatment (not just repainting
    // taskbar+buttons individually) for the same reason that function's
    // own doc comment explains — targeted per-window repaints alone
    // aren't reliably clearing everything here.
    redraw_layers();
}

static void taskbar_paint(mw_handle_t handle, const mw_gl_draw_info_t *d)
{
    (void)handle;
    int16_t menu_h = current_menu_height();

    // Same self-sufficiency fix as miniwin_wince_desktop.c's taskbar_paint():
    // when the Start Menu is open this window grows upward past the
    // taskbar bar, and the area beside the menu popup box needs an
    // explicit desktop-colored fill or it shows stale pixels through.
    if (menu_h > 0) {
        mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
        mw_gl_set_solid_fill_colour(WCE_DESKTOP);
        mw_gl_rectangle(d, 0, 0, (int16_t)SCR_W, menu_h);
    }

    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, menu_h, (int16_t)SCR_W, TASKBAR_H);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_hline(d, 0, (int16_t)(SCR_W - 1), menu_h);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_hline(d, 0, (int16_t)(SCR_W - 1), (int16_t)(menu_h + TASKBAR_H - 1));

    int16_t local_start_y = (int16_t)(menu_h + 2);
    if (s_smenu_open) wince_draw_sunken(d, START_X, local_start_y, START_W, START_H, WCE_BAR);
    else              wince_draw_raised(d, START_X, local_start_y, START_W, START_H, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, START_X + 6, (int16_t)(local_start_y + 5), "Meow!");

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_vline(d, START_X + START_W + 2, local_start_y, (int16_t)(menu_h + TASKBAR_H - 3));
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_vline(d, START_X + START_W + 3, local_start_y, (int16_t)(menu_h + TASKBAR_H - 3));

    {
        int16_t area_x = (int16_t)(START_X + START_W + 6);
        int16_t area_w = (int16_t)((SCR_W - 52) - area_x);
        int n = taskbar_entry_count;
        if (n > 0 && area_w >= 22) {
            int16_t pitch = (int16_t)(area_w / n);
            int16_t bw = (int16_t)(pitch - 2);
            mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
            mw_gl_set_font(MW_GL_FONT_9);
            for (int i = 0; i < n; i++) {
                int16_t bx = (int16_t)(area_x + i * pitch);
                mw_handle_t eh = taskbar_entries[i].handle;
                bool focused = (eh == taskbar_focused_handle) &&
                               !(mw_get_window_flags(eh) & MW_WINDOW_FLAG_IS_MINIMISED);
                if (focused) wince_draw_sunken(d, bx, local_start_y, bw, START_H, WCE_BAR);
                else         wince_draw_raised(d, bx, local_start_y, bw, START_H, WCE_BAR);
                mw_gl_set_fg_colour(WCE_TXT);
                mw_gl_string(d, (int16_t)(bx + 3), (int16_t)(local_start_y + 5), taskbar_entries[i].name);
            }
        }
    }

    // Oversized on purpose — GCC's format-truncation check can't prove
    // "%d.%02uV" stays short even though mv is always a small int in
    // practice; the corner box just visually clips whatever overflows.
    char stat[24];
    if (s_status_show_battery) {
        int mv = purr_kernel_battery_voltage_mv();
        if (mv >= 0) snprintf(stat, sizeof(stat), "%d.%02uV", mv / 1000, (unsigned)((mv % 1000) / 10));
        else         snprintf(stat, sizeof(stat), "no batt");
    } else {
        snprintf(stat, sizeof(stat), "%ukB",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    }
    int16_t bx = SCR_W - 50;
    wince_draw_sunken(d, bx, (int16_t)(menu_h + 2), 48, TASKBAR_H - 4, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, bx + 4, (int16_t)(local_start_y + 5), stat);

    if (!s_smenu_open) return;

    auto draw_sel = [&](int16_t x, int16_t y, int16_t w, int16_t h, bool selected) {
        if (selected && s_smenu_pressed) {
            mw_gl_set_solid_fill_colour(0xFFFFFF);
            mw_gl_rectangle(d, x, y, w, h);
            mw_gl_set_fg_colour(0x000080);
        } else if (selected) {
            mw_gl_set_solid_fill_colour(0x000080);
            mw_gl_rectangle(d, x, y, w, h);
            mw_gl_set_fg_colour(0xFFFFFF);
        } else {
            mw_gl_set_fg_colour(WCE_TXT);
        }
    };

    // Start Menu box — local smy is always 0 (the window's own origin is
    // defined to sit exactly at the menu's screen-absolute top edge), so
    // every "smy" term from the pre-split version simply drops out below.
    if (s_smenu_folder < 0) {
        draw_smenu_box(d, 0, SMENU_TL_H);
        draw_sel(SMENU_X + 1, 2, SMENU_W - 2, SMENU_IH, s_smenu_sel == 0);
        mw_gl_string(d, SMENU_X + 8, 2 + 4, "Programs >");
        mw_gl_set_fg_colour(WCE_SHD);
        mw_gl_hline(d, SMENU_X + 4, SMENU_X + SMENU_W - 4,
                    (int16_t)(2 + SMENU_IH + SMENU_SEP_H / 2));
        draw_sel(SMENU_X + 1, 2 + SMENU_IH + SMENU_SEP_H, SMENU_W - 2, SMENU_IH, s_smenu_sel == 1);
        mw_gl_string(d, SMENU_X + 8,
                     (int16_t)(2 + SMENU_IH + SMENU_SEP_H + 4), "Restart");
    } else {
        smenu_page_t pg = smenu_paginate(s_catalog_count, &s_smenu_page);
        int16_t smh = (int16_t)(pg.max_items * SMENU_IH + 4);
        draw_smenu_box(d, 0, smh);
        draw_sel(SMENU_X + 1, 2, SMENU_W - 2, SMENU_IH, s_smenu_sel == 0);
        mw_gl_string(d, SMENU_X + 8, 2 + 4, "< Back");
        for (int i = 0; i < pg.page_items; i++) {
            int16_t iy = (int16_t)(2 + (i + 1) * SMENU_IH);
            draw_sel(SMENU_X + 1, iy, SMENU_W - 2, SMENU_IH, s_smenu_sel == i + 1);
            mw_gl_string(d, SMENU_X + 8, iy + 4, s_catalog[pg.page_start + i].name);
        }
        if (pg.has_more) {
            int16_t iy = (int16_t)(2 + (pg.page_items + 1) * SMENU_IH);
            draw_sel(SMENU_X + 1, iy, SMENU_W - 2, SMENU_IH, s_smenu_sel == pg.page_items + 1);
            // Oversized on purpose — see draw_title_bar()-style comments
            // elsewhere in this codebase: GCC's format-truncation check
            // can't prove %d stays small even though page counts are tiny
            // in practice.
            char more_lbl[32];
            snprintf(more_lbl, sizeof(more_lbl), "More (%d/%d)", s_smenu_page + 1, pg.page_count);
            mw_gl_string(d, SMENU_X + 8, iy + 4, more_lbl);
        }
    }
}

static void taskbar_message(const mw_message_t *msg)
{
    if (msg->message_id == MW_KEY_PRESSED_MESSAGE) {
        uint8_t code = (uint8_t)msg->message_data;
        if (!s_smenu_open && taskbar_focused_handle != MW_INVALID_HANDLE) {
            mw_post_message(MW_KEY_PRESSED_MESSAGE,
                            MW_INVALID_HANDLE, taskbar_focused_handle,
                            (uint32_t)code, NULL, MW_WINDOW_MESSAGE);
            return;
        }

        // Every smenu-affecting keypress just resizes (a no-op resize if
        // current_menu_height() didn't actually change) and repaints — now
        // that this window is small instead of the whole screen, a full
        // repaint of it is cheap, so there's no need for a separate
        // scoped-vs-full repaint split.
        bool changed = false;

        if (!s_smenu_open) {
            if (code == 0x0D) { s_smenu_open = true; s_smenu_sel = 0; changed = true; }
        } else if (s_smenu_folder < 0) {
            int max_items = 2;
            int prev_sel = s_smenu_sel;
            if (code == 0x01 || code == 0x03) s_smenu_sel = (s_smenu_sel - 1 + max_items) % max_items;
            if (code == 0x02 || code == 0x04) s_smenu_sel = (s_smenu_sel + 1) % max_items;
            if (s_smenu_sel != prev_sel) changed = true;
            if (code == 0x0D) {
                s_smenu_pressed = true;
                mw_paint_window_client(s_taskbar_handle);
                s_smenu_pressed = false;
                if (s_smenu_sel == 0) { s_smenu_folder = 0; s_smenu_sel = 0; s_smenu_page = 0; }
                else { s_smenu_open = false; s_smenu_folder = -1; app_restart_launch(); }
                changed = true;
            }
            if (code == 0x1B || code == 0x03) { s_smenu_open = false; s_smenu_folder = -1; changed = true; }
        } else {
            smenu_page_t pg = smenu_paginate(s_catalog_count, &s_smenu_page);
            int prev_sel = s_smenu_sel;
            if (code == 0x01) s_smenu_sel = (s_smenu_sel - 1 + pg.max_items) % pg.max_items;
            if (code == 0x02) s_smenu_sel = (s_smenu_sel + 1) % pg.max_items;
            if (s_smenu_sel != prev_sel) changed = true;
            if (code == 0x0D) {
                s_smenu_pressed = true;
                mw_paint_window_client(s_taskbar_handle);
                s_smenu_pressed = false;
                if (s_smenu_sel == 0) {
                    s_smenu_folder = -1; s_smenu_sel = 0; s_smenu_page = 0;
                } else if (pg.has_more && s_smenu_sel == pg.max_items - 1) {
                    s_smenu_page = (s_smenu_page + 1) % pg.page_count;   // "More" row — cycle pages
                    s_smenu_sel = 0;
                } else {
                    int idx = pg.page_start + (s_smenu_sel - 1);
                    s_smenu_open = false; s_smenu_folder = -1; s_smenu_page = 0;
                    if (idx >= 0 && idx < s_catalog_count) s_catalog[idx].launch();
                }
                changed = true;
            }
            if (code == 0x1B || code == 0x03) { s_smenu_folder = -1; s_smenu_sel = 0; s_smenu_page = 0; changed = true; }
        }

        if (changed) resize_taskbar_window();
        return;
    }

    if (msg->message_id != MW_TOUCH_DOWN_MESSAGE) return;

    int16_t tx = (int16_t)(msg->message_data >> 16);
    int16_t ty = (int16_t)(msg->message_data & 0xFFFF);
    int16_t menu_h = current_menu_height();

    if (s_smenu_open) {
        // Touch never has a pure "move the highlight without acting" case
        // like keyboard arrow keys do — every tap here either navigates,
        // launches, or closes, so it always just resizes (see this
        // window's top comment) + repaints via resize_taskbar_window().
        if (s_smenu_folder < 0) {
            if (tx >= SMENU_X && tx < SMENU_X + SMENU_W && ty >= 0 && ty < SMENU_TL_H) {
                int rel_y = ty - 2;
                if (rel_y >= 0 && rel_y < SMENU_IH) {
                    s_smenu_folder = 0;
                    s_smenu_page = 0;
                } else if (rel_y >= SMENU_IH + SMENU_SEP_H) {
                    s_smenu_open = false;
                    s_smenu_folder = -1;
                    app_restart_launch();
                }
            } else {
                s_smenu_open = false;
                s_smenu_folder = -1;
            }
        } else {
            smenu_page_t pg = smenu_paginate(s_catalog_count, &s_smenu_page);
            int16_t smh = (int16_t)(pg.max_items * SMENU_IH + 4);
            if (tx >= SMENU_X && tx < SMENU_X + SMENU_W && ty >= 0 && ty < smh) {
                int item = (ty - 2) / SMENU_IH;
                if (item == 0) {
                    s_smenu_folder = -1;
                    s_smenu_page = 0;
                } else if (pg.has_more && item == pg.max_items - 1) {
                    s_smenu_page = (s_smenu_page + 1) % pg.page_count;
                } else {
                    int idx = pg.page_start + (item - 1);
                    s_smenu_open = false;
                    s_smenu_folder = -1;
                    s_smenu_page = 0;
                    if (idx >= 0 && idx < s_catalog_count) {
                        s_catalog[idx].launch();
                        mw_paint_all();
                    }
                }
            } else {
                s_smenu_open = false;
                s_smenu_folder = -1;
            }
        }

        resize_taskbar_window();
        return;
    }

    if (ty >= menu_h && tx >= START_X && tx < START_X + START_W) {
        s_smenu_open = true;
        resize_taskbar_window();
        return;
    }

    {
        int n = taskbar_entry_count;
        int16_t area_x = (int16_t)(START_X + START_W + 6);
        int16_t area_w = (int16_t)((SCR_W - 52) - area_x);
        if (n > 0 && ty >= menu_h && area_w >= 22 &&
            tx >= area_x && tx < (int16_t)(area_x + area_w)) {
            int16_t pitch = (int16_t)(area_w / n);
            int idx = (tx - area_x) / pitch;
            if (idx >= 0 && idx < n) {
                mw_handle_t h = taskbar_entries[idx].handle;
                bool is_min     = (mw_get_window_flags(h) & MW_WINDOW_FLAG_IS_MINIMISED) != 0;
                bool is_focused = (h == taskbar_focused_handle) && !is_min;
                if (is_focused) {
                    mw_set_window_minimised(h, true);
                    taskbar_set_focus(MW_INVALID_HANDLE);
                } else {
                    if (is_min) mw_set_window_minimised(h, false);
                    taskbar_set_focus(h);
                    mw_bring_window_to_front(h);
                }
                mw_paint_all();
            }
        }
    }
}

// ── Window 4: Lock overlay (z=4, created invisible) ──────────────────────────
//
// Always full-screen at origin (0,0) — it doesn't need to resize like the
// taskbar+menu window does. Starts invisible and inert; on_lock_transition()
// below is what makes it visible + brings it to the front of the z-order
// (outranking any currently-open app window) the instant the screen locks,
// and hides it again on unlock. Replaces the old minimize-every-app-window
// workaround — see miniwin_wince_desktop.c's on_lock_transition() for the
// full rationale, mirrored here.

static mw_handle_t s_lock_handle = MW_INVALID_HANDLE;

// Unused externally today (grep confirms no call sites), but kept
// pointing at the taskbar+menu window — the closest analog to "the shell"
// now that there's no single window covering the whole desktop — rather
// than the lock window, which is a very different, mostly-inactive one.
extern "C" mw_handle_t wince_shell_handle(void) { return s_taskbar_handle; }

static void lock_paint(mw_handle_t handle, const mw_gl_draw_info_t *d)
{
    (void)handle;
    const int16_t W = (int16_t)SCR_W;
    const int16_t H = (int16_t)SCR_H;

    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(0x000000);
    mw_gl_rectangle(d, 0, 0, W, H);
    mw_gl_set_fg_colour(0xFFFFFF);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    const char *l1 = "Locked";
    const char *l2 = "Space then Enter,";
    const char *l3 = "or tap below";
    int16_t l1w = mw_gl_get_string_width_pixels(l1);
    int16_t l2w = mw_gl_get_string_width_pixels(l2);
    int16_t l3w = mw_gl_get_string_width_pixels(l3);
    mw_gl_string(d, (int16_t)((W - l1w) / 2), (int16_t)(H / 2 - 30), l1);
    mw_gl_string(d, (int16_t)((W - l2w) / 2), (int16_t)(H / 2 - 14), l2);
    mw_gl_string(d, (int16_t)((W - l3w) / 2), (int16_t)(H / 2), l3);

    mw_util_rect_t hs;
    lock_hotspot_rect(&hs);
    wince_draw_raised(d, hs.x, hs.y, hs.width, hs.height, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    const char *ul = "Unlock";
    int16_t ulw = mw_gl_get_string_width_pixels(ul);
    mw_gl_string(d, (int16_t)(hs.x + (hs.width - ulw) / 2), (int16_t)(hs.y + (hs.height - 9) / 2), ul);

    draw_lock_info(d);
}

static void lock_message(const mw_message_t *msg)
{
    if (msg->message_id != MW_TOUCH_DOWN_MESSAGE) return;
    int16_t tx = (int16_t)(msg->message_data >> 16);
    int16_t ty = (int16_t)(msg->message_data & 0xFFFF);
    mw_util_rect_t hs;
    lock_hotspot_rect(&hs);
    bool in_hotspot = tx >= hs.x && tx < hs.x + hs.width && ty >= hs.y && ty < hs.y + hs.height;
    if (miniwin_lock_handle_touch(in_hotspot)) {
        mw_paint_window_client(s_lock_handle);
    }
}

static void on_lock_transition(bool locked) {
    if (locked) {
        mw_set_window_visible(s_lock_handle, true);
        mw_bring_window_to_front(s_lock_handle);
        mw_paint_window_client(s_lock_handle);
    } else {
        mw_set_window_visible(s_lock_handle, false);
        // Not mw_paint_all() — see redraw_layers()'s doc comment for why
        // that can't be trusted to catch every window here. App windows
        // were never touched by locking (unlike the old minimize-every-
        // window approach this replaced), so they don't need repainting;
        // only the 3 layers the lock overlay was covering do.
        redraw_layers();
    }
}

extern "C" {

void mw_user_init(void)
{
    mw_util_rect_t r;

    mw_util_set_rect(&r, 0, 0, SCR_W, SCR_H);
    s_wallpaper_handle = mw_add_window(&r, "",
        wallpaper_paint, wallpaper_message, NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT,
        NULL);

    mw_util_set_rect(&r, 0, 0, SCR_W, TASKBAR_Y);
    s_dtbtn_handle = mw_add_window(&r, "",
        dtbtn_paint, dtbtn_message, NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT,
        NULL);

    mw_util_set_rect(&r, 0, TASKBAR_Y, SCR_W, TASKBAR_H);
    s_taskbar_handle = mw_add_window(&r, "",
        taskbar_paint, taskbar_message, NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT,
        NULL);

    mw_util_set_rect(&r, 0, 0, SCR_W, SCR_H);
    s_lock_handle = mw_add_window(&r, "",
        lock_paint, lock_message, NULL, 0,
        MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT,   // IS_VISIBLE deliberately omitted — see this window's own comment
        NULL);

    miniwin_lock_set_transition_cb(on_lock_transition);
    mw_paint_all();
}

void mw_user_root_paint_function(const mw_gl_draw_info_t *draw_info)
{
    (void)draw_info;
}

void mw_user_root_message_function(const mw_message_t *message)
{
    (void)message;
}

}  // extern "C"

static TaskHandle_t s_shell_task = NULL;

static void wince_shell_task(void *arg)
{
    (void)arg;

    mw_hal_non_vol_init();
    mw_hal_timer_init();
    mw_hal_lcd_init();
    mw_hal_touch_init();

    mw_init();  // calibrates touch if needed, then calls mw_user_init() above
    miniwin_cursor_init((int)SCR_W, (int)SCR_H);
    miniwin_lock_init();
    ESP_LOGI(TAG, "WinCE shell ready (%dx%d)", (int)SCR_W, (int)SCR_H);

    // Lock-screen footer's own slow, deliberate refresh — deliberately NOT
    // the same 1s cadence as the unlocked RAM/battery corner. The lock
    // overlay otherwise only ever repaints on a real state transition (see
    // on_lock_transition()); this is the one intentional exception, and
    // it's scoped to just the footer rect so it can't reintroduce a
    // whole-screen redraw-every-second.
#define LOCK_INFO_REFRESH_MS 8000
    TickType_t last_lock_info = xTaskGetTickCount();

#define STATUS_ROTATE_TICKS 4
    int status_ticks = 0;
    TickType_t last_repaint = xTaskGetTickCount();
    for (;;) {
        mw_process_message();
        miniwin_keyboard_poll();
        miniwin_cursor_poll();

        TickType_t now = xTaskGetTickCount();
        if ((now - last_repaint) >= pdMS_TO_TICKS(1000)) {
            last_repaint = now;
            miniwin_lock_check_idle();
            // While locked, the overlay owns the screen and repaints itself
            // exactly once per real state change — on_lock_transition()'s
            // immediate mw_paint_window_client() when it fires, and the
            // dismiss handlers' own repaint on unlock. This periodic tick is
            // only for the unlocked RAM/battery corner rotation; skip it
            // entirely while locked instead of redundantly repainting a
            // static overlay every second.
            if (!miniwin_lock_is_locked()) {
                if (++status_ticks >= STATUS_ROTATE_TICKS) {
                    status_ticks = 0;
                    s_status_show_battery = !s_status_show_battery;
                }
                mw_util_rect_t status_rect;
                wce_status_rect(&status_rect);
                mw_paint_window_client_rect(s_taskbar_handle, &status_rect);
            }
        }

        if (miniwin_lock_is_locked() && (now - last_lock_info) >= pdMS_TO_TICKS(LOCK_INFO_REFRESH_MS)) {
            last_lock_info = now;
            mw_util_rect_t info_r;
            lock_info_rect(&info_r);
            mw_paint_window_client_rect(s_lock_handle, &info_r);
        } else if (!miniwin_lock_is_locked()) {
            last_lock_info = now;   // stay caught up so the footer doesn't repaint stale on the next lock
        }
        taskYIELD();
    }
}

void wince_shell_start(void)
{
    miniwin_win_register();
    xTaskCreate(wince_shell_task, "wince_shell", 8192, NULL, 5, &s_shell_task);
}
