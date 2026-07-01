// app_drivers.cpp — Drivers app: SYS and USER (PDL) tabs

#include "app_drivers.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"
#include "purr_sys_drv.h"
#include "purr_drv.h"
#include "partition_manager.h"
#include <string.h>
#include <stdio.h>

static mw_handle_t s_handle = MW_INVALID_HANDLE;

typedef enum { TAB_SYS, TAB_USER, TAB_COUNT } drv_tab_t;
static drv_tab_t s_tab   = TAB_SYS;
static int       s_scroll = 0;

#define WIN_W   260
#define WIN_H   200
#define TAB_H   16
#define TAB_W   (WIN_W / TAB_COUNT)
#define ENTRY_H 13
#define CONTENT_Y (TAB_H + 2)
#define ROWS_VISIBLE ((WIN_H - CONTENT_Y) / ENTRY_H)

static const char *const tab_labels[TAB_COUNT] = { "SYS", "USER" };

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

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);

    mw_gl_set_fill(MW_GL_FILL); mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);

    paint_tab_bar(d);

    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);

    if (s_tab == TAB_SYS) {
        sys_drv_t *drv[SYS_DRV_MAX];
        int n = sys_drv_list(drv, SYS_DRV_MAX);
        if (n == 0) {
            mw_gl_set_fg_colour(WCE_SHD);
            mw_gl_string(d, 6, CONTENT_Y + 4, "No system drivers registered");
        }
        for (int i = s_scroll; i < n && (i - s_scroll) < ROWS_VISIBLE; i++) {
            int16_t y = (int16_t)(CONTENT_Y + (i - s_scroll) * ENTRY_H);
            mw_gl_set_fg_colour(drv[i]->enabled ? WCE_TXT : WCE_SHD);
            char row[52];
            snprintf(row, sizeof(row), "%-18s  %-8s  %s",
                     drv[i]->name, drv[i]->subsystem,
                     drv[i]->enabled ? "on" : "off");
            mw_gl_string(d, 4, y, row);
        }
    } else {
        char names[PURR_DRV_MAX][32];
        int n = purr_drv_list(names, PURR_DRV_MAX);
        if (n == 0) {
            mw_gl_set_fg_colour(WCE_SHD);
            if (!pm_sd_available()) {
                mw_gl_string(d, 6, CONTENT_Y + 4, "No SD card");
                mw_gl_string(d, 6, CONTENT_Y + 17, "No user drivers");
            } else {
                mw_gl_string(d, 6, CONTENT_Y + 4, "No PDL drivers loaded");
                mw_gl_string(d, 6, CONTENT_Y + 17, "Load via: drvmgr load <name>");
            }
        }
        for (int i = s_scroll; i < n && (i - s_scroll) < ROWS_VISIBLE; i++) {
            int16_t y = (int16_t)(CONTENT_Y + (i - s_scroll) * ENTRY_H);
            mw_gl_set_fg_colour(WCE_TXT);
            mw_gl_string(d, 4, y, names[i]);
        }
    }
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        break;

    case MW_TOUCH_DOWN_MESSAGE: {
        mw_util_rect_t cr = mw_get_window_client_rect(msg->recipient_handle);
        int16_t tx = (int16_t)(msg->message_data >> 16);
        int16_t ty = (int16_t)(msg->message_data & 0xFFFF);
        int16_t rx = tx - cr.x;
        int16_t ry = ty - cr.y;

        if (ry >= 0 && ry < TAB_H) {
            int tab = rx / TAB_W;
            if (tab >= 0 && tab < TAB_COUNT) {
                s_tab = (drv_tab_t)tab;
                s_scroll = 0;
                mw_paint_window_client(msg->recipient_handle);
            }
        }
        break;
    }

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
        break;

    default:
        break;
    }
}

void app_drivers_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        return;
    }
    s_tab    = TAB_SYS;
    s_scroll = 0;
    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(WIN_W), 30, WIN_W, WIN_H);
    s_handle = mw_add_window(&r, "Drivers",
        paint, message, NULL, 0, APP_WIN_FLAGS, NULL);
    taskbar_register(s_handle, "Drivers");
}
