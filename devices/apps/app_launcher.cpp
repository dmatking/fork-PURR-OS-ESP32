#include "app_launcher.h"
#include "app_lua_window.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "kitt.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"
#include <dirent.h>
#include <string.h>
#include <cstdio>

extern KITT kitt;

typedef enum { TAB_USER, TAB_ADMIN, TAB_COUNT } app_tab_t;

static mw_handle_t s_handle = MW_INVALID_HANDLE;
static mw_handle_t s_script_win = MW_INVALID_HANDLE;
static app_lua_window_t *s_script = NULL;
static app_tab_t s_tab = TAB_USER;

// Background task tracking
#define MAX_BACKGROUND_TASKS 8
static app_lua_window_t *s_background_tasks[MAX_BACKGROUND_TASKS] = {NULL};
static int s_background_count = 0;

static void add_background_task(app_lua_window_t *w)
{
    if (s_background_count < MAX_BACKGROUND_TASKS)
        s_background_tasks[s_background_count++] = w;
}

static void remove_background_task(app_lua_window_t *w)
{
    for (int i = 0; i < s_background_count; i++) {
        if (s_background_tasks[i] == w) {
            for (int j = i; j < s_background_count - 1; j++)
                s_background_tasks[j] = s_background_tasks[j + 1];
            s_background_count--;
            return;
        }
    }
}

#define WIN_W    240
#define WIN_H    190
#define TAB_H    16
#define TAB_W    (WIN_W / TAB_COUNT)
#define ENTRY_H  13
#define MAX_APPS 32

typedef struct {
    char path[512];
    char name[32];
    bool is_admin;
} app_entry_t;

static app_entry_t s_apps[MAX_APPS];
static int s_app_count = 0;
static int s_scroll = 0;
static const char *const tab_labels[TAB_COUNT] = { "User", "Admin" };

static void paint_tab_bar(const mw_gl_draw_info_t *d)
{
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_12);

    for (int i = 0; i < TAB_COUNT; i++) {
        int16_t tx = (int16_t)(i * TAB_W);
        mw_gl_set_solid_fill_colour(WCE_BAR);
        mw_gl_rectangle(d, tx, 0, TAB_W, TAB_H);
        if (i == (int)s_tab) {
            mw_gl_set_fg_colour(WCE_DARK);
            mw_gl_hline(d, tx, (int16_t)(tx + TAB_W - 1), 0);
            mw_gl_vline(d, tx, 0, (int16_t)(TAB_H - 1));
            mw_gl_set_fg_colour(WCE_HI);
        } else {
            mw_gl_set_fg_colour(WCE_HI);
            mw_gl_hline(d, tx, (int16_t)(tx + TAB_W - 1), 0);
            mw_gl_vline(d, tx, 0, (int16_t)(TAB_H - 1));
            mw_gl_set_fg_colour(WCE_DARK);
        }
        mw_gl_hline(d, tx, (int16_t)(tx + TAB_W - 1), (int16_t)(TAB_H - 1));
        mw_gl_vline(d, (int16_t)(tx + TAB_W - 1), 0, (int16_t)(TAB_H - 1));
        mw_gl_set_fg_colour(WCE_TXT);
        mw_gl_string(d, (int16_t)(tx + 6), 4, tab_labels[i]);
    }
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 0, WIN_W - 1, TAB_H);
}

static void scan_apps(void)
{
    s_app_count = 0;
    s_scroll = 0;

    if (!kitt.sd_available()) return;

    DIR *d = opendir("/sdcard/apps");
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && s_app_count < MAX_APPS) {
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext) continue;

        bool is_admin = false;
        if (strcmp(ext, ".claw") == 0) {
            is_admin = true;
        } else if (strcmp(ext, ".paw") == 0) {
            is_admin = false;
        } else {
            continue;
        }

        app_entry_t *app = &s_apps[s_app_count];
        snprintf(app->path, sizeof(app->path), "/sdcard/apps/%s", ent->d_name);
        strncpy(app->name, ent->d_name, sizeof(app->name) - 1);
        app->name[sizeof(app->name) - 1] = '\0';
        if (app->name[strlen(app->name) - 4] == '.')
            app->name[strlen(app->name) - 4] = '\0';
        app->is_admin = is_admin;
        s_app_count++;
    }
    closedir(d);
}

static void script_paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    if (!s_script) return;
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    app_lua_window_paint(s_script, cr.width, cr.height, (const void *)d);
}

static void script_message(const mw_message_t *msg)
{
    if (!s_script) return;

    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        mw_set_timer(MW_TICKS_PER_SECOND / 10, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    case MW_TIMER_MESSAGE:
        mw_paint_window_client(msg->recipient_handle);
        if (app_lua_window_is_running(s_script))
            mw_set_timer(MW_TICKS_PER_SECOND / 10, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    case MW_TOUCH_DOWN_MESSAGE:
        app_lua_window_on_message(s_script, msg->message_id, msg->message_data);
        break;
    case MW_WINDOW_REMOVED_MESSAGE:
        if (s_script) {
            if (app_lua_window_get_background(s_script)) {
                add_background_task(s_script);
            } else {
                app_lua_window_free(s_script);
            }
        }
        s_script = NULL;
        s_script_win = MW_INVALID_HANDLE;
        taskbar_unregister(s_script_win);
        break;
    default:
        break;
    }
}

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    int16_t cw = cr.width;
    int16_t ch = cr.height;

    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cw, ch);

    paint_tab_bar(d);

    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);

    if (!kitt.sd_available()) {
        mw_gl_set_fg_colour(WCE_SHD);
        mw_gl_string(d, 4, (int16_t)(TAB_H + 10), "SD card not available");
        return;
    }

    int tab_app_count = 0;
    for (int i = 0; i < s_app_count; i++) {
        bool is_admin = s_apps[i].is_admin;
        if ((s_tab == TAB_USER && !is_admin) || (s_tab == TAB_ADMIN && is_admin)) {
            tab_app_count++;
        }
    }

    if (tab_app_count == 0) {
        mw_gl_set_fg_colour(WCE_SHD);
        mw_gl_string(d, 4, (int16_t)(TAB_H + 10), "No apps in this tab");
        mw_gl_string(d, 4, (int16_t)(TAB_H + 26), "Place .paw (user) or");
        mw_gl_string(d, 4, (int16_t)(TAB_H + 40), ".claw (admin) files");
        return;
    }

    int visible = (ch - TAB_H - 2) / ENTRY_H;
    int rendered = 0;
    for (int i = 0; i < s_app_count; i++) {
        app_entry_t *app = &s_apps[i];
        if ((s_tab == TAB_USER && app->is_admin) || (s_tab == TAB_ADMIN && !app->is_admin)) {
            continue;
        }
        if (rendered >= s_scroll && rendered < s_scroll + visible) {
            int16_t y = (int16_t)(TAB_H + 2 + (rendered - s_scroll) * ENTRY_H);
            mw_gl_set_fg_colour(app->is_admin ? 0xFF0000u : WCE_TXT);
            mw_gl_string(d, 4, y, app->name);
            mw_gl_set_fg_colour(WCE_SHD);
            mw_gl_hline(d, 0, (int16_t)(cw - 1), (int16_t)(y + ENTRY_H - 1));
        }
        rendered++;
    }
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        scan_apps();
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        break;

    case MW_TOUCH_DOWN_MESSAGE: {
        int16_t cx = (int16_t)((msg->message_data >> 16) & 0xFFFF);
        int16_t cy = (int16_t)(msg->message_data & 0xFFFF);

        // Check if click is in tab bar
        if (cy < TAB_H) {
            int tab = cx / TAB_W;
            if (tab >= 0 && tab < TAB_COUNT) {
                s_tab = (app_tab_t)tab;
                s_scroll = 0;
                mw_paint_window_client(msg->recipient_handle);
            }
            break;
        }

        if (s_app_count == 0) break;

        // Adjust y to content area below tabs
        int16_t content_y = (int16_t)(cy - TAB_H);

        // Find which app in the current tab was clicked
        int rendered = 0;
        int clicked_idx = -1;
        for (int i = 0; i < s_app_count; i++) {
            if ((s_tab == TAB_USER && s_apps[i].is_admin) ||
                (s_tab == TAB_ADMIN && !s_apps[i].is_admin)) {
                continue;
            }
            if (rendered >= s_scroll && rendered < s_scroll + 20 &&
                content_y >= rendered - s_scroll * ENTRY_H &&
                content_y < (rendered - s_scroll + 1) * ENTRY_H) {
                clicked_idx = i;
                break;
            }
            rendered++;
        }

        if (clicked_idx < 0) break;

        if (s_script_win != MW_INVALID_HANDLE) {
            if (mw_get_window_flags(s_script_win) & MW_WINDOW_FLAG_IS_MINIMISED)
                mw_set_window_minimised(s_script_win, false);
            mw_bring_window_to_front(s_script_win);
            taskbar_set_focus(s_script_win);
            mw_paint_all();
            break;
        }

        app_entry_t *app = &s_apps[clicked_idx];

        s_script = app_lua_window_create(app->path, app->is_admin);
        if (!s_script || !app_lua_window_is_running(s_script)) {
            mw_paint_window_client(msg->recipient_handle);
            break;
        }

        mw_util_rect_t r;
        mw_util_set_rect(&r, APP_WIN_X(WIN_W), 12, WIN_W, WIN_H);
        s_script_win = mw_add_window(&r, app->name,
            script_paint, script_message, NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
        taskbar_register(s_script_win, app->name);
        break;
    }

    case MW_WINDOW_REMOVED_MESSAGE:
        if (s_script) app_lua_window_free(s_script);
        s_script = NULL;
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
        break;

    default:
        break;
    }
}

void app_launcher_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        taskbar_set_focus(s_handle);
        mw_paint_all();
        return;
    }
    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(WIN_W), 12, WIN_W, WIN_H);
    s_handle = mw_add_window(&r, "Apps",
        paint, message, NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
    taskbar_register(s_handle, "Apps");
}

// Background task accessors (for task manager app)
app_lua_window_t **app_launcher_get_background_tasks(int *count)
{
    if (count) *count = s_background_count;
    return s_background_tasks;
}

void app_launcher_remove_background_task(app_lua_window_t *w)
{
    remove_background_task(w);
}
