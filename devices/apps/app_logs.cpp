#include "app_logs.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "purr_log.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"

static mw_handle_t s_handle = MW_INVALID_HANDLE;

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);

    if (purr_log_count == 0) {
        mw_gl_string(d, 4, 6, "No log entries yet.");
        return;
    }
    int start = (purr_log_head - purr_log_count + PURR_LOG_LINES) % PURR_LOG_LINES;
    for (int i = 0; i < purr_log_count; i++) {
        int16_t y = (int16_t)(4 + i * 13);
        if (y + 10 > cr.height) break;
        mw_gl_string(d, 2, y, purr_log_ring[(start + i) % PURR_LOG_LINES]);
    }
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        purr_log_hook_install();
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        /* fall through */
    case MW_TIMER_MESSAGE:
        mw_paint_window_client(msg->recipient_handle);
        mw_set_timer(MW_TICKS_PER_SECOND, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;
    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
        break;
    default:
        break;
    }
}

void app_logs_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        return;
    }
    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(230), 20, 230, 170);
    s_handle = mw_add_window(&r, "Logs",
        paint, message, NULL, 0, APP_WIN_FLAGS, NULL);
    taskbar_register(s_handle, "Logs");
}
