// shell_blackberry.cpp — Blackberry-inspired MiniWin shell for PURR OS
// Green-on-black terminal theme. Active when PURR_THEME_BLACKBERRY=1.
// Layout (320x240): status(16) | time(24) | notif(12) | content(142) | tabs(14) | dock(32)

#ifdef PURR_THEME_BLACKBERRY

#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include "purr_app_catalog.h"
#include "purr_taskbar.h"
#include "app_settings.h"
#include "app_files.h"
#include "kitt.h"
#include "purr_version.h"

#ifdef PURR_HAS_LUA
#include "app_lua_window.h"
#endif

extern KITT kitt;

// ── Green-on-black palette (RGB888, MiniWin converts to native 16-bit) ────────
#define BB_BLACK      0x000000u
#define BB_BG_TINT    0x000800u  // near-black with slight green tint
#define BB_PANEL      0x001800u  // dark green panel
#define BB_PLATE_BG   0x000C00u  // darker green for logo plate
#define BB_SEP        0x003000u  // separator lines
#define BB_GREEN_HI   0x00FF44u  // phosphor green — primary text/active
#define BB_GREEN      0x00CC22u  // normal green
#define BB_GREEN_MID  0x008818u  // dimmed green — secondary text
#define BB_GREEN_DIM  0x004410u  // inactive / background tint
#define BB_ICON_BG    0x002200u  // dark green icon tile
#define BB_ADMIN_BG   0x220000u  // admin (.claw) apps — slight red tint

// ── Layout ────────────────────────────────────────────────────────────────────
#define SCR_W       mw_hal_lcd_get_display_width()
#define SCR_H       mw_hal_lcd_get_display_height()

#define STATUS_H    16
#define TIME_H      24
#define NOTIF_H     12
#define TAB_H       14
#define DOCK_H      32
#define CONTENT_Y   (STATUS_H + TIME_H + NOTIF_H)
#define CONTENT_H   (SCR_H - CONTENT_Y - TAB_H - DOCK_H)
#define TAB_Y       (SCR_H - DOCK_H - TAB_H)
#define DOCK_Y      (SCR_H - DOCK_H)

#define TAB_COUNT   3
#define DOCK_COLS   4
#define GRID_COLS   4
#define GRID_CELL_W (SCR_W / GRID_COLS)
#define GRID_CELL_H 44

// ── State ─────────────────────────────────────────────────────────────────────
typedef enum { BB_HOME, BB_DRAWER, BB_TASKS, BB_SYSTEM } bb_state_t;

static mw_handle_t  shell_handle;
static bb_state_t   bb_state        = BB_HOME;
static int          bb_tab          = 1;  // "All" default
static bool         bb_reboot_armed = false;  // true after first reboot tap

// ── App list ──────────────────────────────────────────────────────────────────
#define MAX_BB_APPS 16

typedef struct {
    char    name[24];
    char    path[128];
    bool    is_admin;
    bool    is_builtin;
    uint8_t catalog_idx;
} bb_app_t;

static bb_app_t bb_apps[MAX_BB_APPS];
static int      bb_napp = 0;

static void bb_scan_apps(void)
{
    bb_napp = 0;

    for (int i = 0; i < purr_catalog_count && bb_napp < MAX_BB_APPS; i++) {
        bb_app_t *a = &bb_apps[bb_napp++];
        strncpy(a->name, purr_catalog[i].name, sizeof(a->name) - 1);
        a->name[sizeof(a->name) - 1] = '\0';
        a->path[0]     = '\0';
        a->is_builtin  = true;
        a->is_admin    = false;
        a->catalog_idx = (uint8_t)i;
    }

    if (!kitt.sd_available()) return;
    DIR *d = opendir("/sdcard/apps");
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && bb_napp < MAX_BB_APPS) {
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext) continue;
        bool admin = (strcmp(ext, ".claw") == 0);
        if (!admin && strcmp(ext, ".paws") != 0) continue;
        bb_app_t *a = &bb_apps[bb_napp++];
        snprintf(a->path, sizeof(a->path), "/sdcard/apps/%.114s", ent->d_name);
        strncpy(a->name, ent->d_name, sizeof(a->name) - 1);
        a->name[sizeof(a->name) - 1] = '\0';
        char *dot = strrchr(a->name, '.');
        if (dot) *dot = '\0';
        a->is_builtin  = false;
        a->is_admin    = admin;
        a->catalog_idx = 0;
    }
    closedir(d);
}

// ── Lua window (SD apps only) ─────────────────────────────────────────────────
#ifdef PURR_HAS_LUA

static mw_handle_t       s_lua_win    = MW_INVALID_HANDLE;
static app_lua_window_t *s_lua_script = NULL;

static void lua_win_paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    if (!s_lua_script) return;
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    app_lua_window_paint(s_lua_script, cr.width, cr.height, d);
}

static void lua_win_message(const mw_message_t *msg)
{
    if (!s_lua_script) return;
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        mw_set_timer(MW_TICKS_PER_SECOND / 10, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    case MW_TIMER_MESSAGE:
        mw_paint_window_client(msg->recipient_handle);
        if (app_lua_window_is_running(s_lua_script))
            mw_set_timer(MW_TICKS_PER_SECOND / 10, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    case MW_TOUCH_DOWN_MESSAGE:
        app_lua_window_on_message(s_lua_script, msg->message_id, msg->message_data);
        break;
    case MW_WINDOW_REMOVED_MESSAGE:
        app_lua_window_free(s_lua_script);
        s_lua_script = NULL;
        s_lua_win    = MW_INVALID_HANDLE;
        break;
    default:
        break;
    }
}

static void bb_launch_lua(bb_app_t *app)
{
    if (s_lua_win != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_lua_win) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_lua_win, false);
        mw_bring_window_to_front(s_lua_win);
        mw_paint_all();
        return;
    }
    s_lua_script = app_lua_window_create(app->path, app->is_admin);
    if (!s_lua_script || !app_lua_window_is_running(s_lua_script)) return;

    mw_util_rect_t r;
    mw_util_set_rect(&r, 10, 14, 300, 210);
    s_lua_win = mw_add_window(&r, app->name,
        lua_win_paint, lua_win_message, NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_HAS_TITLE_BAR |
        MW_WINDOW_FLAG_CAN_BE_CLOSED | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT,
        NULL);
}

#endif // PURR_HAS_LUA

static void bb_launch_app(bb_app_t *app)
{
    if (app->is_builtin) {
        purr_catalog[app->catalog_idx].launch();
        return;
    }
#ifdef PURR_HAS_LUA
    bb_launch_lua(app);
#endif
}

// ── Draw helpers ──────────────────────────────────────────────────────────────

static void bb_fill(const mw_gl_draw_info_t *d,
                    int16_t x, int16_t y, int16_t w, int16_t h,
                    mw_hal_lcd_colour_t color)
{
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(color);
    mw_gl_rectangle(d, x, y, w, h);
}

static void bb_text(const mw_gl_draw_info_t *d,
                    int16_t x, int16_t y, const char *s,
                    mw_hal_lcd_colour_t fg)
{
    mw_gl_set_fg_colour(fg);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, x, y, s);
}

// ── Paint sections ────────────────────────────────────────────────────────────

static void paint_status(const mw_gl_draw_info_t *d)
{
    bb_fill(d, 0, 0, SCR_W, STATUS_H, BB_BLACK);
    bb_text(d, 4, 4, "PURR OS " PURR_OS_VERSION, BB_GREEN_MID);

    bool wifi = kitt.wifi_connected();
    const char *wifi_s = wifi ? "NET:UP" : "NET:--";
    bb_text(d, (int16_t)(SCR_W - 44), 4, wifi_s,
            wifi ? BB_GREEN_HI : BB_GREEN_DIM);
}

static void paint_timebar(const mw_gl_draw_info_t *d)
{
    bb_fill(d, 0, STATUS_H, SCR_W, TIME_H, BB_PANEL);

    uint32_t up_s  = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t hh    = up_s / 3600;
    uint32_t mm    = (up_s % 3600) / 60;
    uint32_t ss    = up_s % 60;
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu:%02lu",
             (unsigned long)(hh > 99 ? 99 : hh),
             (unsigned long)mm,
             (unsigned long)ss);

    // Centered time
    int16_t tw = (int16_t)(strlen(tbuf) * 6);
    bb_text(d, (int16_t)((SCR_W - tw) / 2), (int16_t)(STATUS_H + 4), tbuf, BB_GREEN_HI);

    // Device name dimly on right
    const char *dname = kitt.device_name();
    if (dname && dname[0]) {
        int16_t dnw = (int16_t)(strlen(dname) * 6);
        bb_text(d, (int16_t)(SCR_W - dnw - 4), (int16_t)(STATUS_H + 13), dname, BB_GREEN_DIM);
    }
}

static void paint_notif(const mw_gl_draw_info_t *d)
{
    bb_fill(d, 0, STATUS_H + TIME_H, SCR_W, NOTIF_H, BB_BG_TINT);
    unsigned free_kb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024);
    char nbuf[28];
    snprintf(nbuf, sizeof(nbuf), "RAM %u kB free", free_kb);
    bb_text(d, 4, (int16_t)(STATUS_H + TIME_H + 2), nbuf, BB_GREEN_DIM);
}

static void paint_wallpaper(const mw_gl_draw_info_t *d)
{
    bb_fill(d, 0, CONTENT_Y, SCR_W, CONTENT_H, BB_BG_TINT);

    // Logo plate
    int16_t pw = 150, ph = 52;
    int16_t px = (int16_t)((SCR_W - pw) / 2);
    int16_t py = (int16_t)(CONTENT_Y + (CONTENT_H - ph) / 2);
    bb_fill(d, px, py, pw, ph, BB_PLATE_BG);
    mw_gl_set_fg_colour(BB_SEP);
    mw_gl_set_fill(MW_GL_NO_FILL);
    mw_gl_set_border(MW_GL_BORDER_ON);
    mw_gl_rectangle(d, px, py, pw, ph);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);

    bb_text(d, (int16_t)(px + 28), (int16_t)(py + 8),  "[ PURR OS ]",    BB_GREEN_HI);
    bb_text(d, (int16_t)(px + 40), (int16_t)(py + 22), "v" PURR_OS_VERSION, BB_GREEN_MID);
    bb_text(d, (int16_t)(px + 20), (int16_t)(py + 36), "tap to open apps", BB_GREEN_DIM);
}

static void paint_tabs(const mw_gl_draw_info_t *d)
{
    static const char *const TABS[TAB_COUNT] = { "Tasks", "All", "System" };
    int16_t tw = (int16_t)(SCR_W / TAB_COUNT);

    bb_fill(d, 0, TAB_Y, SCR_W, TAB_H, BB_BLACK);

    // Separator above tabs
    mw_gl_set_fg_colour(BB_SEP);
    mw_gl_hline(d, 0, (int16_t)(SCR_W - 1), TAB_Y);

    for (int i = 0; i < TAB_COUNT; i++) {
        int16_t tx  = (int16_t)(i * tw);
        bool    act = (i == bb_tab);
        mw_hal_lcd_colour_t fg = act ? BB_GREEN_HI : BB_GREEN_DIM;
        if (act) {
            mw_gl_set_fg_colour(BB_GREEN);
            mw_gl_hline(d, (int16_t)(tx + 1), (int16_t)(tx + tw - 2),
                        (int16_t)(TAB_Y + TAB_H - 1));
        }
        int16_t lx = (int16_t)(tx + (tw - (int16_t)(strlen(TABS[i]) * 6)) / 2);
        bb_text(d, lx, (int16_t)(TAB_Y + 3), TABS[i], fg);
    }
}

static void paint_dock(const mw_gl_draw_info_t *d)
{
    static const char *const LABELS[DOCK_COLS] = { "APPS", "FILES", "SETT", "BOOT" };

    bb_fill(d, 0, DOCK_Y, SCR_W, DOCK_H, BB_BLACK);
    mw_gl_set_fg_colour(BB_SEP);
    mw_gl_hline(d, 0, (int16_t)(SCR_W - 1), DOCK_Y);

    int16_t bw = (int16_t)(SCR_W / DOCK_COLS - 4);
    int16_t bh = (int16_t)(DOCK_H - 6);

    for (int i = 0; i < DOCK_COLS; i++) {
        int16_t bx = (int16_t)(i * (SCR_W / DOCK_COLS) + 2);
        int16_t by = (int16_t)(DOCK_Y + 3);

        mw_hal_lcd_colour_t bg = (i == 3) ? BB_ADMIN_BG : BB_ICON_BG;
        bb_fill(d, bx, by, bw, bh, bg);

        // Green border
        mw_gl_set_fg_colour(BB_SEP);
        mw_gl_set_fill(MW_GL_NO_FILL);
        mw_gl_set_border(MW_GL_BORDER_ON);
        mw_gl_rectangle(d, bx, by, bw, bh);
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_set_border(MW_GL_BORDER_OFF);

        int16_t lx = (int16_t)(bx + (bw - (int16_t)(strlen(LABELS[i]) * 6)) / 2);
        int16_t ly = (int16_t)(by + (bh - 9) / 2);
        bb_text(d, lx, ly, LABELS[i], BB_GREEN_HI);
    }
}

static void paint_system(const mw_gl_draw_info_t *d)
{
    bb_fill(d, 0, CONTENT_Y, SCR_W, CONTENT_H + TAB_H, BB_BLACK);

    bb_text(d, 4, (int16_t)(CONTENT_Y + 2), "[ system ]", BB_GREEN_MID);
    mw_gl_set_fg_colour(BB_SEP);
    mw_gl_hline(d, 0, (int16_t)(SCR_W - 1), (int16_t)(CONTENT_Y + 14));

    // Reboot button row
    int16_t bx = 8, by = (int16_t)(CONTENT_Y + 22), bw = (int16_t)(SCR_W - 16), bh = 26;

    if (bb_reboot_armed) {
        // Armed state — red background, confirm prompt
        bb_fill(d, bx, by, bw, bh, BB_ADMIN_BG);
        mw_gl_set_fg_colour(0xFF4444u);
        mw_gl_set_fill(MW_GL_NO_FILL);
        mw_gl_set_border(MW_GL_BORDER_ON);
        mw_gl_rectangle(d, bx, by, bw, bh);
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_set_border(MW_GL_BORDER_OFF);
        int16_t lx = (int16_t)(bx + (bw - (int16_t)(strlen("!! TAP AGAIN TO REBOOT !!") * 6)) / 2);
        bb_text(d, lx, (int16_t)(by + 8), "!! TAP AGAIN TO REBOOT !!", 0xFF4444u);

        // Cancel hint
        bb_text(d, 8, (int16_t)(by + bh + 8), "tap elsewhere to cancel", BB_GREEN_DIM);
    } else {
        // Normal state — green reboot button
        bb_fill(d, bx, by, bw, bh, BB_ICON_BG);
        mw_gl_set_fg_colour(BB_SEP);
        mw_gl_set_fill(MW_GL_NO_FILL);
        mw_gl_set_border(MW_GL_BORDER_ON);
        mw_gl_rectangle(d, bx, by, bw, bh);
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_set_border(MW_GL_BORDER_OFF);
        int16_t lx = (int16_t)(bx + (bw - (int16_t)(strlen("Reboot") * 6)) / 2);
        bb_text(d, lx, (int16_t)(by + 8), "Reboot", BB_GREEN_HI);
    }
}

#define TASK_ROW_H  20

static void paint_tasks(const mw_gl_draw_info_t *d)
{
    bb_fill(d, 0, CONTENT_Y, SCR_W, CONTENT_H + TAB_H, BB_BLACK);

    bb_text(d, 4, (int16_t)(CONTENT_Y + 2), "[ running tasks ]", BB_GREEN_MID);
    mw_gl_set_fg_colour(BB_SEP);
    mw_gl_hline(d, 0, (int16_t)(SCR_W - 1), (int16_t)(CONTENT_Y + 14));

    if (taskbar_entry_count == 0) {
        bb_text(d, 8, (int16_t)(CONTENT_Y + 24), "No running apps", BB_GREEN_DIM);
        return;
    }

    int16_t ry = (int16_t)(CONTENT_Y + 18);
    for (int i = 0; i < taskbar_entry_count && ry < (DOCK_Y - TASK_ROW_H); i++) {
        bool focused = (taskbar_entries[i].handle == taskbar_focused_handle);

        // Row background for focused item
        if (focused)
            bb_fill(d, 0, ry, SCR_W, TASK_ROW_H, BB_PANEL);

        // Bullet
        mw_hal_lcd_colour_t bullet_col = focused ? BB_GREEN_HI : BB_GREEN_MID;
        bb_text(d, 4, (int16_t)(ry + 5), focused ? ">" : "-", bullet_col);

        // App name
        bb_text(d, 16, (int16_t)(ry + 5), taskbar_entries[i].name,
                focused ? BB_GREEN_HI : BB_GREEN);

        // Focused badge
        if (focused)
            bb_text(d, (int16_t)(SCR_W - 44), (int16_t)(ry + 5), "[focus]", BB_GREEN_DIM);

        // Row separator
        mw_gl_set_fg_colour(BB_SEP);
        mw_gl_hline(d, 0, (int16_t)(SCR_W - 1), (int16_t)(ry + TASK_ROW_H - 1));

        ry += TASK_ROW_H;
    }
}

static void paint_drawer(const mw_gl_draw_info_t *d)
{
    bb_fill(d, 0, CONTENT_Y, SCR_W, CONTENT_H + TAB_H, BB_BLACK);

    // Close bar
    bb_text(d, 4, (int16_t)(CONTENT_Y + 2), "[ close ]", BB_GREEN_MID);
    mw_gl_set_fg_colour(BB_SEP);
    mw_gl_hline(d, 0, (int16_t)(SCR_W - 1), (int16_t)(CONTENT_Y + 14));

    int16_t grid_y0 = (int16_t)(CONTENT_Y + 16);
    int     max_vis = (CONTENT_H + TAB_H - 16) / GRID_CELL_H;
    int     row = 0, col = 0;

    for (int i = 0; i < bb_napp && row < max_vis; i++) {
        int16_t cx = (int16_t)(col * GRID_CELL_W);
        int16_t cy = (int16_t)(grid_y0 + row * GRID_CELL_H);
        int16_t iw = (int16_t)(GRID_CELL_W - 4);
        int16_t ih = (int16_t)(GRID_CELL_H - 14);

        mw_hal_lcd_colour_t ic = bb_apps[i].is_admin ? BB_ADMIN_BG : BB_ICON_BG;
        bb_fill(d, (int16_t)(cx + 2), (int16_t)(cy + 2), iw, ih, ic);

        // Green border on icon
        mw_gl_set_fg_colour(BB_SEP);
        mw_gl_set_fill(MW_GL_NO_FILL);
        mw_gl_set_border(MW_GL_BORDER_ON);
        mw_gl_rectangle(d, (int16_t)(cx + 2), (int16_t)(cy + 2), iw, ih);
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_set_border(MW_GL_BORDER_OFF);

        // 2-letter abbreviation
        char ab[3] = { bb_apps[i].name[0],
                       bb_apps[i].name[1] ? bb_apps[i].name[1] : ' ', '\0' };
        bb_text(d, (int16_t)(cx + (GRID_CELL_W - 12) / 2),
                (int16_t)(cy + 2 + (ih - 9) / 2), ab, BB_GREEN_HI);

        // Name label
        int16_t max_chars = (int16_t)(GRID_CELL_W / 6);
        char label[9] = {};
        strncpy(label, bb_apps[i].name, (size_t)(max_chars < 8 ? max_chars : 8));
        int16_t lx = (int16_t)(cx + (GRID_CELL_W - (int16_t)(strlen(label) * 6)) / 2);
        bb_text(d, lx > cx ? lx : cx, (int16_t)(cy + ih + 4), label, BB_GREEN_MID);

        if (++col >= GRID_COLS) { col = 0; row++; }
    }
}

// ── Main paint ────────────────────────────────────────────────────────────────

static void shell_paint(mw_handle_t handle, const mw_gl_draw_info_t *d)
{
    (void)handle;
    paint_status(d);
    paint_timebar(d);
    paint_notif(d);
    if (bb_state == BB_DRAWER)      paint_drawer(d);
    else if (bb_state == BB_TASKS)  paint_tasks(d);
    else if (bb_state == BB_SYSTEM) paint_system(d);
    else                            paint_wallpaper(d);
    paint_tabs(d);
    paint_dock(d);
}

// ── Touch ─────────────────────────────────────────────────────────────────────

static void handle_dock(int col)
{
    switch (col) {
    case 0:  // APPS — toggle drawer
        bb_reboot_armed = false;
        bb_state = (bb_state == BB_DRAWER) ? BB_HOME : BB_DRAWER;
        if (bb_state == BB_DRAWER) bb_scan_apps();
        break;
    case 1:  app_files_launch();    break;
    case 2:  app_settings_launch(); break;
    case 3:  esp_restart();         break;
    }
}

static void shell_message(const mw_message_t *msg)
{
    if (msg->message_id == MW_WINDOW_CREATED_MESSAGE ||
        msg->message_id == MW_TIMER_MESSAGE) {
        mw_paint_window_client(shell_handle);
        mw_set_timer(MW_TICKS_PER_SECOND, shell_handle, MW_WINDOW_MESSAGE);
        return;
    }
    if (msg->message_id != MW_TOUCH_DOWN_MESSAGE) return;

    int16_t tx = (int16_t)(msg->message_data >> 16);
    int16_t ty = (int16_t)(msg->message_data & 0xFFFF);

    // Dock
    if (ty >= DOCK_Y) {
        int col = tx / (SCR_W / DOCK_COLS);
        if (col >= 0 && col < DOCK_COLS) handle_dock(col);
        mw_paint_window_client(shell_handle);
        return;
    }

    // Tabs
    if (ty >= TAB_Y) {
        int tab = tx / (SCR_W / TAB_COUNT);
        if (tab >= 0 && tab < TAB_COUNT) {
            bb_tab = tab;
            if (tab == 0) {
                bb_state = (bb_state == BB_TASKS) ? BB_HOME : BB_TASKS;
                bb_reboot_armed = false;
            } else if (tab == 2) {
                bb_state = (bb_state == BB_SYSTEM) ? BB_HOME : BB_SYSTEM;
                bb_reboot_armed = false;
            } else {
                // All tab — open app drawer
                if (bb_state != BB_DRAWER) {
                    bb_state = BB_DRAWER;
                    bb_scan_apps();
                }
            }
        }
        mw_paint_window_client(shell_handle);
        return;
    }

    // Content area
    if (ty >= CONTENT_Y && ty < TAB_Y) {
        if (bb_state == BB_SYSTEM) {
            // Hit-test the reboot button (bx=8, by=CONTENT_Y+22, bw=SCR_W-16, bh=26)
            int16_t bx = 8, by = (int16_t)(CONTENT_Y + 22);
            int16_t bw = (int16_t)(SCR_W - 16), bh = 26;
            bool hit_button = (tx >= bx && tx < bx + bw && ty >= by && ty < by + bh);
            if (hit_button) {
                if (bb_reboot_armed) {
                    esp_restart();
                } else {
                    bb_reboot_armed = true;
                }
            } else {
                // Tap anywhere else — cancel armed state
                bb_reboot_armed = false;
            }
            mw_paint_window_client(shell_handle);
        } else if (bb_state == BB_TASKS) {
            // Tap close bar
            if (ty < CONTENT_Y + 16) {
                bb_state = BB_HOME;
                mw_paint_window_client(shell_handle);
                return;
            }
            // Tap a task row
            int16_t grid_y0 = (int16_t)(CONTENT_Y + 18);
            int idx = (ty - grid_y0) / TASK_ROW_H;
            if (idx >= 0 && idx < taskbar_entry_count) {
                bb_state = BB_HOME;
                mw_paint_window_client(shell_handle);
                mw_bring_window_to_front(taskbar_entries[idx].handle);
                mw_paint_all();
            }
        } else if (bb_state == BB_DRAWER) {
            // Tap close bar
            if (ty < CONTENT_Y + 16) {
                bb_state = BB_HOME;
                mw_paint_window_client(shell_handle);
                return;
            }
            // Tap app icon
            int16_t grid_y0 = (int16_t)(CONTENT_Y + 16);
            int row = (ty - grid_y0) / GRID_CELL_H;
            int col = tx / GRID_CELL_W;
            int idx = row * GRID_COLS + col;
            if (idx >= 0 && idx < bb_napp) {
                bb_state = BB_HOME;
                mw_paint_window_client(shell_handle);
                bb_launch_app(&bb_apps[idx]);
                mw_paint_all();
            }
        } else {
            bb_state = BB_DRAWER;
            bb_scan_apps();
            mw_paint_window_client(shell_handle);
        }
    }
}

// ── MiniWin entry points ───────────────────────────────────────────────────────

extern "C" {

void mw_user_init(void)
{
    mw_util_rect_t r;
    mw_util_set_rect(&r, 0, 0, SCR_W, SCR_H);
    shell_handle = mw_add_window(&r, "",
        shell_paint, shell_message, NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT,
        NULL);
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

} // extern "C"

#endif // PURR_THEME_BLACKBERRY
