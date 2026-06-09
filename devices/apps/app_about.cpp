#include "app_about.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "kitt.h"
#include "purr_version.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include "purr_apps_common.h"
#include "purr_taskbar.h"

extern KITT kitt;

static mw_handle_t s_handle = MW_INVALID_HANDLE;

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);
    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_12);
    mw_gl_string(d, 8,  8, "PURR OS  " PURR_OS_VERSION);
    mw_gl_string(d, 8, 22, kitt.device_name());
    mw_gl_string(d, 8, 36, "KITT  " KITT_VERSION);
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 4, cr.width - 4, 50);
    char buf[26];
    snprintf(buf, sizeof(buf), "Free RAM: %ukB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_string(d, 8, 58, buf);
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
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

void app_about_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        return;
    }
    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(200), 35, 200, 90);
    s_handle = mw_add_window(&r, "About PURR OS",
        paint, message, NULL, 0, APP_WIN_FLAGS, NULL);
    taskbar_register(s_handle, "About");
}
