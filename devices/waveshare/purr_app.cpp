// Generic WCE shell — placeholder for non-CYD targets.
// Uses stub LCD/touch HAL; renders nothing until real drivers are wired up.

#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>

// ── Layout (driven by HAL at runtime) ────────────────────────────────────────
#define SCR_W           mw_hal_lcd_get_display_width()
#define SCR_H           mw_hal_lcd_get_display_height()
#define TASKBAR_H       22
#define TASKBAR_Y       (SCR_H - TASKBAR_H)

#define START_X         2
#define START_Y         (TASKBAR_Y + 2)
#define START_W         52
#define START_H         (TASKBAR_H - 4)

#define SMENU_W         130
#define SMENU_ITEM_H    18
#define SMENU_ITEMS     5
#define SMENU_H         (SMENU_ITEMS * SMENU_ITEM_H + 4)
#define SMENU_X         0
#define SMENU_Y         (TASKBAR_Y - SMENU_H)

#define ABOUT_X         60
#define ABOUT_Y         60
#define ABOUT_W         200
#define ABOUT_H         110

// ── Windows CE palette ────────────────────────────────────────────────────────
#define WCE_DESKTOP     0x008080
#define WCE_BAR         0xC0C0C0
#define WCE_HILIGHT     0xFFFFFF
#define WCE_SHADOW      0x808080
#define WCE_DARK_SHD    0x404040
#define WCE_TEXT        0x000000
#define WCE_MENU_BG     0xD4D0C8
#define WCE_MENU_TXT    0x000000
#define WCE_MENU_SEP    0x808080
#define WCE_SEL_BG      0x000080
#define WCE_SEL_TXT     0xFFFFFF

// ── State ─────────────────────────────────────────────────────────────────────
static mw_handle_t shell_handle;
static bool smenu_open = false;
static bool about_open = false;

static const char *smenu_labels[SMENU_ITEMS] = {
    "Programs",
    "Settings",
    "About PURR OS",
    "-",
    "Shut Down...",
};

// ── 3D helpers ────────────────────────────────────────────────────────────────
static void draw_raised(const mw_gl_draw_info_t *d,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        mw_hal_lcd_colour_t fill)
{
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(fill);
    mw_gl_rectangle(d, x, y, w, h);
    mw_gl_set_fg_colour(WCE_HILIGHT);
    mw_gl_hline(d, x,     x+w-1, y);
    mw_gl_vline(d, x,     y,     y+h-1);
    mw_gl_set_fg_colour(WCE_SHADOW);
    mw_gl_hline(d, x+1,   x+w-2, y+h-2);
    mw_gl_vline(d, x+w-2, y+1,   y+h-2);
    mw_gl_set_fg_colour(WCE_DARK_SHD);
    mw_gl_hline(d, x,     x+w-1, y+h-1);
    mw_gl_vline(d, x+w-1, y,     y+h-1);
}

static void draw_sunken(const mw_gl_draw_info_t *d,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        mw_hal_lcd_colour_t fill)
{
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(fill);
    mw_gl_rectangle(d, x, y, w, h);
    mw_gl_set_fg_colour(WCE_DARK_SHD);
    mw_gl_hline(d, x,     x+w-1, y);
    mw_gl_vline(d, x,     y,     y+h-1);
    mw_gl_set_fg_colour(WCE_SHADOW);
    mw_gl_hline(d, x+1,   x+w-2, y+1);
    mw_gl_vline(d, x+1,   y+1,   y+h-2);
    mw_gl_set_fg_colour(WCE_HILIGHT);
    mw_gl_hline(d, x,     x+w-1, y+h-1);
    mw_gl_vline(d, x+w-1, y,     y+h-1);
}

// ── Paint ─────────────────────────────────────────────────────────────────────
static void shell_paint(mw_handle_t handle, const mw_gl_draw_info_t *d)
{
    (void)handle;

    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_DESKTOP);
    mw_gl_rectangle(d, 0, 0, SCR_W, TASKBAR_Y);

    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, TASKBAR_Y, SCR_W, TASKBAR_H);
    mw_gl_set_fg_colour(WCE_HILIGHT);
    mw_gl_hline(d, 0, SCR_W - 1, TASKBAR_Y);
    mw_gl_set_fg_colour(WCE_DARK_SHD);
    mw_gl_hline(d, 0, SCR_W - 1, SCR_H - 1);

    if (smenu_open)
        draw_sunken(d, START_X, START_Y, START_W, START_H, WCE_BAR);
    else
        draw_raised(d, START_X, START_Y, START_W, START_H, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TEXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, START_X + 10, START_Y + 5, "Start");

    mw_gl_set_fg_colour(WCE_SHADOW);
    mw_gl_vline(d, START_X + START_W + 2, TASKBAR_Y + 2, SCR_H - 3);
    mw_gl_set_fg_colour(WCE_HILIGHT);
    mw_gl_vline(d, START_X + START_W + 3, TASKBAR_Y + 2, SCR_H - 3);

    uint32_t free_kb = esp_get_free_heap_size() / 1024;
    char ram_buf[10];
    snprintf(ram_buf, sizeof(ram_buf), "%ukB", (unsigned)free_kb);
    int16_t box_x = (int16_t)(SCR_W - 50);
    draw_sunken(d, box_x, TASKBAR_Y + 2, 48, TASKBAR_H - 4, WCE_BAR);
    mw_gl_set_fg_colour(WCE_TEXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_string(d, box_x + 4, START_Y + 5, ram_buf);

    if (smenu_open) {
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_set_border(MW_GL_BORDER_OFF);
        mw_gl_set_solid_fill_colour(WCE_MENU_BG);
        mw_gl_rectangle(d, SMENU_X, SMENU_Y, SMENU_W, SMENU_H);
        mw_gl_set_fg_colour(WCE_DARK_SHD);
        mw_gl_set_fill(MW_GL_NO_FILL);
        mw_gl_set_border(MW_GL_BORDER_ON);
        mw_gl_rectangle(d, SMENU_X, SMENU_Y, SMENU_W, SMENU_H);

        mw_gl_set_font(MW_GL_FONT_9);
        for (int i = 0; i < SMENU_ITEMS; i++) {
            int16_t iy = SMENU_Y + 2 + i * SMENU_ITEM_H;
            if (smenu_labels[i][0] == '-') {
                mw_gl_set_fg_colour(WCE_MENU_SEP);
                mw_gl_set_fill(MW_GL_FILL);
                mw_gl_set_border(MW_GL_BORDER_OFF);
                mw_gl_hline(d, SMENU_X + 4, SMENU_X + SMENU_W - 4,
                            iy + SMENU_ITEM_H / 2);
            } else {
                mw_gl_set_fg_colour(WCE_MENU_TXT);
                mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
                mw_gl_string(d, SMENU_X + 8, iy + 4, smenu_labels[i]);
            }
        }
    }

    if (about_open) {
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_set_border(MW_GL_BORDER_OFF);
        mw_gl_set_solid_fill_colour(WCE_DARK_SHD);
        mw_gl_rectangle(d, ABOUT_X + 3, ABOUT_Y + 3, ABOUT_W, ABOUT_H);

        draw_raised(d, ABOUT_X, ABOUT_Y, ABOUT_W, ABOUT_H, WCE_BAR);

        mw_gl_set_solid_fill_colour(WCE_SEL_BG);
        mw_gl_set_fill(MW_GL_FILL);
        mw_gl_set_border(MW_GL_BORDER_OFF);
        mw_gl_rectangle(d, ABOUT_X + 2, ABOUT_Y + 2, ABOUT_W - 4, 16);
        mw_gl_set_fg_colour(WCE_SEL_TXT);
        mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
        mw_gl_set_font(MW_GL_FONT_9);
        mw_gl_string(d, ABOUT_X + 5, ABOUT_Y + 4, "About PURR OS");

        mw_gl_set_fg_colour(WCE_TEXT);
        mw_gl_string(d, ABOUT_X + 8, ABOUT_Y + 26, "PURR OS  v0.9.1");
        mw_gl_string(d, ABOUT_X + 8, ABOUT_Y + 42, "Generic target");

        char ram_line[20];
        snprintf(ram_line, sizeof(ram_line), "Free RAM: %ukB",
                 (unsigned)(esp_get_free_heap_size() / 1024));
        mw_gl_string(d, ABOUT_X + 8, ABOUT_Y + 58, ram_line);

        mw_gl_set_fg_colour(WCE_SHADOW);
        mw_gl_hline(d, ABOUT_X + 4, ABOUT_X + ABOUT_W - 4, ABOUT_Y + 76);

        mw_gl_set_fg_colour(WCE_TEXT);
        mw_gl_string(d, ABOUT_X + 8, ABOUT_Y + 84, "Tap anywhere to close");
    }
}

// ── Actions ───────────────────────────────────────────────────────────────────
static void smenu_fire_action(int item)
{
    switch (item) {
    case 2: about_open = true;  break;
    case 4: esp_restart();      break;
    default: break;
    }
}

// ── Message ───────────────────────────────────────────────────────────────────
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

    if (about_open) {
        about_open = false;
        mw_paint_window_client(shell_handle);
        return;
    }

    if (smenu_open) {
        if (tx >= SMENU_X && tx < SMENU_X + SMENU_W &&
            ty >= SMENU_Y && ty < SMENU_Y + SMENU_H) {
            int item = (ty - SMENU_Y - 2) / SMENU_ITEM_H;
            if (item >= 0 && item < SMENU_ITEMS &&
                smenu_labels[item][0] != '-') {
                smenu_open = false;
                smenu_fire_action(item);
            } else {
                smenu_open = false;
            }
        } else {
            smenu_open = false;
        }
        mw_paint_window_client(shell_handle);
        return;
    }

    if (ty >= TASKBAR_Y) {
        if (tx >= START_X && tx < START_X + START_W) {
            smenu_open = true;
            mw_paint_window_client(shell_handle);
        }
    }
}

// ── Entry points ──────────────────────────────────────────────────────────────
extern "C" {

void mw_user_init(void)
{
    mw_util_rect_t r;
    mw_util_set_rect(&r, 0, 0, (int16_t)SCR_W, (int16_t)SCR_H);
    shell_handle = mw_add_window(
        &r, "",
        shell_paint, shell_message,
        NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT,
        NULL
    );
    mw_paint_all();
}

void mw_user_root_paint_function(const mw_gl_draw_info_t *draw_info)
{
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_DESKTOP);
    mw_gl_rectangle(draw_info, 0, 0, (int16_t)SCR_W, (int16_t)SCR_H);
}

void mw_user_root_message_function(const mw_message_t *message)
{
    (void)message;
}

} // extern "C"
