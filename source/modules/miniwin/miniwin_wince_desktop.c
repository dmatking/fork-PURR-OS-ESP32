// miniwin_wince_desktop.c — WinCE-style taskbar+start-menu desktop for the
// generic MiniWin .purr module. Ported from the WinCE shell baked directly
// into kernel_tdeck_plus_arduino (wince_shell/common/taskbar.cpp), generalized
// so any native-kernel MiniWin device can use it: the Start Menu's Programs
// list is built from the real app_manager registry instead of a fixed
// catalog, and taskbar entries are registered automatically by every window
// that goes through purr_win_create() (see the wce_taskbar_* calls in
// miniwin_win.c) rather than by hand in each app.
//
// Only compiled in when CONFIG_PURR_MINIWIN_DESKTOP_WINCE is set.
#include "sdkconfig.h"

#ifdef CONFIG_PURR_MINIWIN_DESKTOP_WINCE

#include "miniwin_wince_desktop.h"
#include "MiniWin/miniwin_utilities.h"
#include "MiniWin/gl/gl.h"
#include "MiniWin/hal/hal_lcd.h"
#include "../../kernel/core/purr_kernel.h"
#include "../app_manager/app_manager.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

// ── Palette + bevels (ported from wince_common.cpp) ─────────────────────────

#define WCE_DESKTOP 0x008080
#define WCE_BAR     0xC0C0C0
#define WCE_HI      0xFFFFFF
#define WCE_SHD     0x808080
#define WCE_DARK    0x404040
#define WCE_TXT     0x000000
#define WCE_MBKG    0xD4D0C8

#define TASKBAR_H   22

static void wce_draw_raised(const mw_gl_draw_info_t *d,
                             int16_t x, int16_t y, int16_t w, int16_t h, uint32_t fill) {
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(fill); mw_gl_rectangle(d, x, y, w, h);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_hline(d, x, x+w-1, y); mw_gl_vline(d, x, y, y+h-1);
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, x+1, x+w-2, y+h-2); mw_gl_vline(d, x+w-2, y+1, y+h-2);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_hline(d, x, x+w-1, y+h-1); mw_gl_vline(d, x+w-1, y, y+h-1);
}

static void wce_draw_sunken(const mw_gl_draw_info_t *d,
                             int16_t x, int16_t y, int16_t w, int16_t h, uint32_t fill) {
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(fill); mw_gl_rectangle(d, x, y, w, h);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_hline(d, x, x+w-1, y); mw_gl_vline(d, x, y, y+h-1);
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, x+1, x+w-2, y+1); mw_gl_vline(d, x+1, y+1, y+h-2);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_hline(d, x, x+w-1, y+h-1); mw_gl_vline(d, x+w-1, y, y+h-1);
}

// ── Taskbar registry (ported from wince_taskbar.cpp) ────────────────────────

#define TASKBAR_MAX_ENTRIES 8

typedef struct { mw_handle_t handle; char name[12]; } wce_taskbar_entry_t;

static wce_taskbar_entry_t s_taskbar_entries[TASKBAR_MAX_ENTRIES];
static int         s_taskbar_count = 0;
static mw_handle_t s_taskbar_focused = MW_INVALID_HANDLE;

void wce_taskbar_register(mw_handle_t handle, const char *name) {
    if (s_taskbar_count >= TASKBAR_MAX_ENTRIES) return;
    s_taskbar_entries[s_taskbar_count].handle = handle;
    strncpy(s_taskbar_entries[s_taskbar_count].name, name ? name : "",
            sizeof(s_taskbar_entries[0].name) - 1);
    s_taskbar_entries[s_taskbar_count].name[sizeof(s_taskbar_entries[0].name) - 1] = '\0';
    s_taskbar_count++;
    s_taskbar_focused = handle;
}

void wce_taskbar_unregister(mw_handle_t handle) {
    if (s_taskbar_focused == handle) s_taskbar_focused = MW_INVALID_HANDLE;
    for (int i = 0; i < s_taskbar_count; i++) {
        if (s_taskbar_entries[i].handle == handle) {
            for (int j = i; j < s_taskbar_count - 1; j++)
                s_taskbar_entries[j] = s_taskbar_entries[j + 1];
            s_taskbar_count--;
            return;
        }
    }
}

// ── Desktop window ───────────────────────────────────────────────────────────

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
#define SMENU_TL_H  (3 * SMENU_IH + SMENU_SEP_H + 4)
#define SMENU_MAX_PROGRAMS 10
#define SMENU_MAX_NOTIFS   8

static mw_handle_t s_desktop_handle = MW_INVALID_HANDLE;
static bool s_smenu_open    = false;
static int  s_smenu_folder  = -1;  // -1 = top level, 0 = programs
static int  s_smenu_sel     = 0;
static bool s_smenu_pressed = false;

mw_handle_t wce_desktop_handle(void) { return s_desktop_handle; }

int16_t wce_taskbar_height(void) { return TASKBAR_H; }

static int programs_count(void) {
    int n = app_manager_count();
    if (n > SMENU_MAX_PROGRAMS) n = SMENU_MAX_PROGRAMS;
    return n;
}

static int notifs_count(void) {
    int n = purr_kernel_notify_count();
    if (n > SMENU_MAX_NOTIFS) n = SMENU_MAX_NOTIFS;
    return n;
}

static void draw_sel(const mw_gl_draw_info_t *d, int16_t x, int16_t y, int16_t w, int16_t h,
                      bool selected, bool pressed) {
    if (selected && pressed) {
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
}

static void draw_smenu_box(const mw_gl_draw_info_t *d, int16_t smy, int16_t smh) {
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

static void desktop_paint(mw_handle_t handle, const mw_gl_draw_info_t *d) {
    (void)handle;
    const int16_t W = (int16_t)SCR_W;
    const int16_t H = (int16_t)SCR_H;

    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_DESKTOP);
    mw_gl_rectangle(d, 0, 0, W, H);

    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, TASKBAR_Y, SCR_W, TASKBAR_H);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_hline(d, 0, SCR_W - 1, TASKBAR_Y);
    mw_gl_set_fg_colour(WCE_DARK);
    mw_gl_hline(d, 0, SCR_W - 1, SCR_H - 1);

    if (s_smenu_open) wce_draw_sunken(d, START_X, START_Y, START_W, START_H, WCE_BAR);
    else              wce_draw_raised(d, START_X, START_Y, START_W, START_H, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, START_X + 6, START_Y + 5, "Meow!");

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_vline(d, START_X + START_W + 2, TASKBAR_Y + 2, SCR_H - 3);
    mw_gl_set_fg_colour(WCE_HI);
    mw_gl_vline(d, START_X + START_W + 3, TASKBAR_Y + 2, SCR_H - 3);

    {
        int16_t area_x = (int16_t)(START_X + START_W + 6);
        int16_t area_w = (int16_t)((SCR_W - 52) - area_x);
        int n = s_taskbar_count;
        if (n > 0 && area_w >= 22) {
            int16_t pitch = (int16_t)(area_w / n);
            int16_t bw = (int16_t)(pitch - 2);
            mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
            mw_gl_set_font(MW_GL_FONT_9);
            for (int i = 0; i < n; i++) {
                int16_t bx = (int16_t)(area_x + i * pitch);
                mw_handle_t eh = s_taskbar_entries[i].handle;
                bool focused = (eh == s_taskbar_focused) &&
                               !(mw_get_window_flags(eh) & MW_WINDOW_FLAG_IS_MINIMISED);
                if (focused) wce_draw_sunken(d, bx, START_Y, bw, START_H, WCE_BAR);
                else         wce_draw_raised(d, bx, START_Y, bw, START_H, WCE_BAR);
                mw_gl_set_fg_colour(WCE_TXT);
                mw_gl_string(d, (int16_t)(bx + 3), START_Y + 5, s_taskbar_entries[i].name);
            }
        }
    }

    char ram[10];
    snprintf(ram, sizeof(ram), "%ukB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    int16_t bx = SCR_W - 50;
    wce_draw_sunken(d, bx, TASKBAR_Y + 2, 48, TASKBAR_H - 4, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, bx + 4, START_Y + 5, ram);

    if (!s_smenu_open) return;

    if (s_smenu_folder < 0) {
        int16_t smy = (int16_t)(TASKBAR_Y - SMENU_TL_H);
        draw_smenu_box(d, smy, SMENU_TL_H);
        draw_sel(d, SMENU_X + 1, smy + 2, SMENU_W - 2, SMENU_IH, s_smenu_sel == 0, s_smenu_pressed);
        mw_gl_string(d, SMENU_X + 8, smy + 2 + 4, "Programs >");
        draw_sel(d, SMENU_X + 1, smy + 2 + SMENU_IH, SMENU_W - 2, SMENU_IH,
                  s_smenu_sel == 1, s_smenu_pressed);
        {
            int nn = purr_kernel_notify_count();
            char nlbl[32];
            if (nn > 0) snprintf(nlbl, sizeof(nlbl), "Notifications (%d) >", nn);
            else        snprintf(nlbl, sizeof(nlbl), "Notifications >");
            mw_gl_string(d, SMENU_X + 8, (int16_t)(smy + 2 + SMENU_IH + 4), nlbl);
        }
        mw_gl_set_fg_colour(WCE_SHD);
        mw_gl_hline(d, SMENU_X + 4, SMENU_X + SMENU_W - 4,
                    (int16_t)(smy + 2 + 2 * SMENU_IH + SMENU_SEP_H / 2));
        draw_sel(d, SMENU_X + 1, smy + 2 + 2 * SMENU_IH + SMENU_SEP_H, SMENU_W - 2, SMENU_IH,
                  s_smenu_sel == 2, s_smenu_pressed);
        mw_gl_string(d, SMENU_X + 8,
                     (int16_t)(smy + 2 + 2 * SMENU_IH + SMENU_SEP_H + 4), "Restart");
    } else if (s_smenu_folder == 0) {
        int count = programs_count();
        int16_t smh = (int16_t)((count + 1) * SMENU_IH + 4);
        int16_t smy = (int16_t)(TASKBAR_Y - smh);
        draw_smenu_box(d, smy, smh);
        draw_sel(d, SMENU_X + 1, smy + 2, SMENU_W - 2, SMENU_IH, s_smenu_sel == 0, s_smenu_pressed);
        mw_gl_string(d, SMENU_X + 8, smy + 2 + 4, "< Back");
        for (int i = 0; i < count; i++) {
            const app_entry_t *app = app_manager_get(i);
            int16_t iy = smy + 2 + (int16_t)((i + 1) * SMENU_IH);
            draw_sel(d, SMENU_X + 1, iy, SMENU_W - 2, SMENU_IH, s_smenu_sel == i + 1, s_smenu_pressed);
            mw_gl_string(d, SMENU_X + 8, iy + 4, app ? app->name : "?");
        }
    } else {
        // Notifications — see purr_kernel_notify() in purr_kernel.h. Title
        // only (SMENU_W is narrow); "< Back" doubles as "Clear all" via the
        // handler when there's at least one notification, same one-item
        // economy the rest of this menu already uses.
        int count = notifs_count();
        int16_t smh = (int16_t)((count + 1) * SMENU_IH + 4);
        int16_t smy = (int16_t)(TASKBAR_Y - smh);
        draw_smenu_box(d, smy, smh);
        draw_sel(d, SMENU_X + 1, smy + 2, SMENU_W - 2, SMENU_IH, s_smenu_sel == 0, s_smenu_pressed);
        mw_gl_string(d, SMENU_X + 8, smy + 2 + 4, count > 0 ? "< Back (clear all)" : "< Back");
        for (int i = 0; i < count; i++) {
            purr_notification_t note;
            int16_t iy = smy + 2 + (int16_t)((i + 1) * SMENU_IH);
            draw_sel(d, SMENU_X + 1, iy, SMENU_W - 2, SMENU_IH, s_smenu_sel == i + 1, s_smenu_pressed);
            mw_gl_string(d, SMENU_X + 8, iy + 4, purr_kernel_notify_at(i, &note) ? note.title : "?");
        }
    }
}

static void desktop_message(const mw_message_t *msg) {
    if (msg->message_id == MW_KEY_PRESSED_MESSAGE) {
        uint8_t code = (uint8_t)msg->message_data;
        if (!s_smenu_open && s_taskbar_focused != MW_INVALID_HANDLE) {
            mw_post_message(MW_KEY_PRESSED_MESSAGE,
                            MW_INVALID_HANDLE, s_taskbar_focused,
                            (uint32_t)code, NULL, MW_WINDOW_MESSAGE);
            return;
        }
        if (!s_smenu_open) {
            if (code == 0x0D) { s_smenu_open = true; s_smenu_sel = 0; }
        } else if (s_smenu_folder < 0) {
            int max_items = 3;
            if (code == 0x01 || code == 0x03) s_smenu_sel = (s_smenu_sel - 1 + max_items) % max_items;
            if (code == 0x02 || code == 0x04) s_smenu_sel = (s_smenu_sel + 1) % max_items;
            if (code == 0x0D) {
                s_smenu_pressed = true;
                mw_paint_window_client(s_desktop_handle);
                s_smenu_pressed = false;
                if (s_smenu_sel == 0)      { s_smenu_folder = 0; s_smenu_sel = 0; }
                else if (s_smenu_sel == 1) { s_smenu_folder = 1; s_smenu_sel = 0; }
                else { s_smenu_open = false; s_smenu_folder = -1; purr_kernel_reboot(); }
            }
            if (code == 0x1B || code == 0x03) { s_smenu_open = false; s_smenu_folder = -1; }
        } else if (s_smenu_folder == 0) {
            int count = programs_count();
            int max_items = count + 1;
            if (code == 0x01) s_smenu_sel = (s_smenu_sel - 1 + max_items) % max_items;
            if (code == 0x02) s_smenu_sel = (s_smenu_sel + 1) % max_items;
            if (code == 0x0D) {
                s_smenu_pressed = true;
                mw_paint_window_client(s_desktop_handle);
                s_smenu_pressed = false;
                if (s_smenu_sel == 0) { s_smenu_folder = -1; s_smenu_sel = 0; }
                else {
                    int idx = s_smenu_sel - 1;
                    s_smenu_open = false; s_smenu_folder = -1;
                    if (idx >= 0 && idx < count) app_manager_launch_idx(idx);
                }
            }
            if (code == 0x1B || code == 0x03) { s_smenu_folder = -1; s_smenu_sel = 0; }
        } else {
            // Notifications folder
            int count = notifs_count();
            int max_items = count + 1;
            if (code == 0x01) s_smenu_sel = (s_smenu_sel - 1 + max_items) % max_items;
            if (code == 0x02) s_smenu_sel = (s_smenu_sel + 1) % max_items;
            if (code == 0x0D) {
                s_smenu_pressed = true;
                mw_paint_window_client(s_desktop_handle);
                s_smenu_pressed = false;
                if (s_smenu_sel == 0) {
                    if (count > 0) purr_kernel_notify_clear();
                    s_smenu_folder = -1; s_smenu_sel = 0;
                }
                // Tapping an individual notification does nothing further —
                // there's no per-notification detail view yet.
            }
            if (code == 0x1B || code == 0x03) { s_smenu_folder = -1; s_smenu_sel = 0; }
        }
        mw_paint_window_client(s_desktop_handle);
        return;
    }

    if (msg->message_id != MW_TOUCH_DOWN_MESSAGE) return;

    int16_t tx = (int16_t)(msg->message_data >> 16);
    int16_t ty = (int16_t)(msg->message_data & 0xFFFF);

    if (s_smenu_open) {
        if (s_smenu_folder < 0) {
            int16_t smy = (int16_t)(TASKBAR_Y - SMENU_TL_H);
            if (tx >= SMENU_X && tx < SMENU_X + SMENU_W && ty >= smy && ty < smy + SMENU_TL_H) {
                int rel_y = ty - smy - 2;
                if (rel_y >= 0 && rel_y < SMENU_IH) {
                    s_smenu_folder = 0;
                } else if (rel_y >= SMENU_IH && rel_y < 2 * SMENU_IH) {
                    s_smenu_folder = 1;
                } else if (rel_y >= 2 * SMENU_IH + SMENU_SEP_H) {
                    s_smenu_open = false;
                    s_smenu_folder = -1;
                    purr_kernel_reboot();
                }
            } else {
                s_smenu_open = false;
                s_smenu_folder = -1;
            }
        } else if (s_smenu_folder == 0) {
            int count = programs_count();
            int16_t smh = (int16_t)((count + 1) * SMENU_IH + 4);
            int16_t smy = (int16_t)(TASKBAR_Y - smh);
            if (tx >= SMENU_X && tx < SMENU_X + SMENU_W && ty >= smy && ty < smy + smh) {
                int item = (ty - smy - 2) / SMENU_IH;
                if (item == 0) {
                    s_smenu_folder = -1;
                } else {
                    int idx = item - 1;
                    s_smenu_open = false;
                    s_smenu_folder = -1;
                    if (idx >= 0 && idx < count) app_manager_launch_idx(idx);
                }
            } else {
                s_smenu_open = false;
                s_smenu_folder = -1;
            }
        } else {
            // Notifications folder — tapping "< Back" clears (if any exist);
            // tapping an individual entry just navigates back for now.
            int count = notifs_count();
            int16_t smh = (int16_t)((count + 1) * SMENU_IH + 4);
            int16_t smy = (int16_t)(TASKBAR_Y - smh);
            if (tx >= SMENU_X && tx < SMENU_X + SMENU_W && ty >= smy && ty < smy + smh) {
                int item = (ty - smy - 2) / SMENU_IH;
                if (item == 0 && count > 0) purr_kernel_notify_clear();
                s_smenu_folder = -1;
            } else {
                s_smenu_open = false;
                s_smenu_folder = -1;
            }
        }
        mw_paint_window_client(s_desktop_handle);
        return;
    }

    if (ty >= TASKBAR_Y && tx >= START_X && tx < START_X + START_W) {
        s_smenu_open = true;
        mw_paint_window_client(s_desktop_handle);
        return;
    }

    {
        int n = s_taskbar_count;
        int16_t area_x = (int16_t)(START_X + START_W + 6);
        int16_t area_w = (int16_t)((SCR_W - 52) - area_x);
        if (n > 0 && ty >= TASKBAR_Y && area_w >= 22 &&
            tx >= area_x && tx < (int16_t)(area_x + area_w)) {
            int16_t pitch = (int16_t)(area_w / n);
            int idx = (tx - area_x) / pitch;
            if (idx >= 0 && idx < n) {
                mw_handle_t h = s_taskbar_entries[idx].handle;
                bool is_min     = (mw_get_window_flags(h) & MW_WINDOW_FLAG_IS_MINIMISED) != 0;
                bool is_focused = (h == s_taskbar_focused) && !is_min;
                if (is_focused) {
                    mw_set_window_minimised(h, true);
                    s_taskbar_focused = MW_INVALID_HANDLE;
                } else {
                    if (is_min) mw_set_window_minimised(h, false);
                    s_taskbar_focused = h;
                    mw_bring_window_to_front(h);
                }
                mw_paint_all();
            }
        }
    }
}

// ── Root hooks (no-ops — the desktop window handles all rendering) ─────────

void mw_user_init(void) {
    mw_util_rect_t r;
    mw_util_set_rect(&r, 0, 0, SCR_W, SCR_H);
    s_desktop_handle = mw_add_window(&r, "",
        desktop_paint, desktop_message, NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT,
        NULL);
    mw_paint_all();
}

void mw_user_root_paint_function(const mw_gl_draw_info_t *draw_info) { (void)draw_info; }
void mw_user_root_message_function(const mw_message_t *message) { (void)message; }

#endif  // CONFIG_PURR_MINIWIN_DESKTOP_WINCE
