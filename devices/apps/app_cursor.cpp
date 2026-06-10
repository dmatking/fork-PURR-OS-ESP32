// Fullscreen cursor window
// Always-on-top, draws mouse cursor, synthesizes clicks at cursor position

#include "app_cursor.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "hal_input.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cursor";

static mw_handle_t s_cursor_win = MW_INVALID_HANDLE;
static int16_t s_cursor_x = 160, s_cursor_y = 120;
static bool s_click_posted = false;

#define CURSOR_SIZE 8

static void paint_cursor(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    // Get updated cursor position from HAL
    hal_input_get_cursor(&s_cursor_x, &s_cursor_y);

    // Draw crosshair cursor with black outline and white fill
    mw_gl_set_fg_colour(0x000000);
    mw_gl_set_border(MW_GL_BORDER_ON);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_rectangle(d, s_cursor_x - CURSOR_SIZE/2, s_cursor_y - CURSOR_SIZE/2,
                    s_cursor_x + CURSOR_SIZE/2, s_cursor_y + CURSOR_SIZE/2);

    // White fill
    mw_gl_set_fg_colour(0xFFFFFF);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_rectangle(d, s_cursor_x - CURSOR_SIZE/2 + 1, s_cursor_y - CURSOR_SIZE/2 + 1,
                    s_cursor_x + CURSOR_SIZE/2 - 1, s_cursor_y + CURSOR_SIZE/2 - 1);

    // Crosshair
    mw_gl_set_fg_colour(0x000000);
    mw_gl_set_fill(MW_GL_NO_FILL);
    mw_gl_vline(d, s_cursor_x, s_cursor_y - CURSOR_SIZE/2, s_cursor_y + CURSOR_SIZE/2);
    mw_gl_hline(d, s_cursor_x - CURSOR_SIZE/2, s_cursor_x + CURSOR_SIZE/2, s_cursor_y);
}

static void message_handler(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        // Set window to be always-on-top
        mw_set_window_always_on_top(msg->recipient_handle, true);
        mw_paint_window_client(msg->recipient_handle);
        break;

    case MW_TIMER_MESSAGE:
        // Poll trackball and update cursor
        hal_input_tick();

        // Check for trackball click
        if (hal_input_click_pending() && !s_click_posted) {
            s_click_posted = true;

            // Post synthetic touch-down at cursor position
            uint32_t touch_data = ((uint32_t)(uint16_t)s_cursor_x << 16) | (uint16_t)s_cursor_y;
            mw_post_message(MW_TOUCH_DOWN_MESSAGE, MW_INVALID_HANDLE, MW_INVALID_HANDLE,
                           touch_data, NULL, MW_WINDOW_MESSAGE);

            ESP_LOGD(TAG, "click at (%d, %d)", s_cursor_x, s_cursor_y);
        } else if (!hal_input_click_pending()) {
            s_click_posted = false;
        }

        mw_paint_window_client(msg->recipient_handle);
        mw_set_timer(MW_TICKS_PER_SECOND / 15, msg->recipient_handle, MW_WINDOW_MESSAGE);
        break;

    default:
        break;
    }
}

void app_cursor_init(void)
{
    if (s_cursor_win != MW_INVALID_HANDLE) return;

    // Create fullscreen cursor window
    mw_util_rect_t r;
    mw_util_set_rect(&r, 0, 0, mw_hal_lcd_get_display_width(), mw_hal_lcd_get_display_height());

    s_cursor_win = mw_add_window(&r, "Cursor",
        paint_cursor, message_handler, NULL, 0, 0, NULL);

    if (s_cursor_win != MW_INVALID_HANDLE) {
        mw_set_window_always_on_top(s_cursor_win, true);
        ESP_LOGI(TAG, "cursor window created");
    }
}
