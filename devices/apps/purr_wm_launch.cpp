// purr_wm_launch.cpp — MiniWin implementation of purr_wm_launch()
// Replaces the stub in ui_stubs.cpp when PURR_HAS_LUA is defined.

#ifdef PURR_HAS_LUA

#include "app_lua_window.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include <string.h>

static mw_handle_t       s_pwm_win    = MW_INVALID_HANDLE;
static app_lua_window_t *s_pwm_script = NULL;

static void pwm_paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    if (!s_pwm_script) return;
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    app_lua_window_paint(s_pwm_script, cr.width, cr.height, d);
}

static void pwm_message(const mw_message_t *msg)
{
    if (!s_pwm_script) return;
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        mw_set_timer(MW_TICKS_PER_SECOND / 10, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    case MW_TIMER_MESSAGE:
        mw_paint_window_client(msg->recipient_handle);
        if (app_lua_window_is_running(s_pwm_script))
            mw_set_timer(MW_TICKS_PER_SECOND / 10, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    case MW_TOUCH_DOWN_MESSAGE:
        app_lua_window_on_message(s_pwm_script, msg->message_id, msg->message_data);
        break;
    case MW_WINDOW_REMOVED_MESSAGE:
        app_lua_window_free(s_pwm_script);
        s_pwm_script = NULL;
        s_pwm_win    = MW_INVALID_HANDLE;
        break;
    default:
        break;
    }
}

extern "C" bool purr_wm_launch(const char *path)
{
    if (!path || path[0] == '\0') return false;

    if (s_pwm_win != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_pwm_win) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_pwm_win, false);
        mw_bring_window_to_front(s_pwm_win);
        mw_paint_all();
        return true;
    }

    const char *ext   = strrchr(path, '.');
    bool        admin = ext && strcmp(ext, ".claw") == 0;

    s_pwm_script = app_lua_window_create(path, admin);
    if (!s_pwm_script || !app_lua_window_is_running(s_pwm_script))
        return false;

    const char *slash = strrchr(path, '/');
    const char *name  = slash ? slash + 1 : path;
    char title[32];
    strncpy(title, name, sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    char *dot = strrchr(title, '.');
    if (dot) *dot = '\0';

    mw_util_rect_t r;
    mw_util_set_rect(&r, 10, 14, 300, 210);
    s_pwm_win = mw_add_window(&r, title,
        pwm_paint, pwm_message, NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_HAS_TITLE_BAR |
        MW_WINDOW_FLAG_CAN_BE_CLOSED | MW_WINDOW_FLAG_TOUCH_INT_AND_EXT,
        NULL);
    return true;
}

#else // PURR_HAS_LUA not defined — stub

extern "C" bool purr_wm_launch(const char *path)
{
    (void)path;
    return false;
}

#endif // PURR_HAS_LUA
