// minwin_hal_adapter.cpp — MiniWin HAL implementation via LVGL
// Routes MiniWin drawing calls through LVGL's proven flush callback.
// This gives us MiniWin's windowing + LVGL's reliable SPI handling.

#ifdef PURR_HAS_LVGL

#include "miniwin.h"
#include "display_ili9341.h"
#include <lvgl.h>
#include "../purr_idf_compat.h"

// Global LVGL display and draw buffer (initialized in main.cpp)
extern lv_disp_t* lv_disp_default();

static lv_color_t* lv_draw_buf = nullptr;
static const uint32_t LV_BUF_SIZE = 320 * 10;  // 320×10 line buffer

void mw_hal_lcd_init(void) {
    display_ili9341_init();  // Initialize TFT_eSPI first

    if (!lv_draw_buf) {
        lv_draw_buf = (lv_color_t*)malloc(LV_BUF_SIZE * sizeof(lv_color_t));
    }
}

int16_t mw_hal_lcd_get_display_width(void) {
    return 320;
}

int16_t mw_hal_lcd_get_display_height(void) {
    return 240;
}

// Convert MiniWin colour (0x00RRGGBB) to LVGL colour
static lv_color_t mw_to_lv_color(mw_hal_lcd_colour_t c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;
    return lv_color_make(r, g, b);
}

void mw_hal_lcd_pixel(int16_t x, int16_t y, mw_hal_lcd_colour_t colour) {
    lv_color_t lv_col = mw_to_lv_color(colour);
    lv_canvas_t* canvas = (lv_canvas_t*)lv_disp_get_layer_top(lv_disp_default());

    // Fallback: use direct draw if canvas not available
    if (!canvas) {
        display_ili9341_fill_rect(x, y, 1, 1, (uint16_t)lv_color_to565(lv_col));
    } else {
        lv_canvas_set_px(canvas, x, y, lv_col);
    }
}

void mw_hal_lcd_filled_rectangle(int16_t start_x, int16_t start_y,
                                  int16_t width, int16_t height,
                                  mw_hal_lcd_colour_t colour) {
    lv_color_t lv_col = mw_to_lv_color(colour);
    lv_area_t area = {start_x, start_y, start_x + width - 1, start_y + height - 1};

    // Draw filled rectangle via LVGL
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.color = lv_col;

    // Use LVGL's draw API to fill the area
    // This will eventually call display_ili9341_flush_cb() for the SPI transfer
    lv_draw_fill(&area, &fill_dsc);
}

void mw_hal_lcd_monochrome_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                        uint16_t bitmap_width, uint16_t bitmap_height,
                                        int16_t clip_start_x, int16_t clip_start_y,
                                        int16_t clip_width, int16_t clip_height,
                                        mw_hal_lcd_colour_t fg_colour,
                                        mw_hal_lcd_colour_t bg_colour,
                                        const uint8_t *image_data) {
    lv_color_t fg = mw_to_lv_color(fg_colour);
    lv_color_t bg = mw_to_lv_color(bg_colour);

    // Draw each pixel from the monochrome bitmap
    uint16_t stride = (bitmap_width + 7) / 8;
    for (int16_t row = 0; row < (int16_t)bitmap_height; row++) {
        int16_t sy = image_start_y + row;
        if (sy < clip_start_y || sy >= clip_start_y + clip_height) continue;

        for (int16_t col = 0; col < (int16_t)bitmap_width; col++) {
            int16_t sx = image_start_x + col;
            if (sx < clip_start_x || sx >= clip_start_x + clip_width) continue;

            bool set = (image_data[row * stride + col / 8] >> (7 - (col & 7))) & 1;
            lv_color_t c = set ? fg : bg;

            // Plot pixel via LVGL or direct draw
            mw_hal_lcd_pixel(sx, sy, set ? fg_colour : bg_colour);
        }
    }
}

void mw_hal_lcd_colour_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                    uint16_t bitmap_width, uint16_t bitmap_height,
                                    int16_t clip_start_x, int16_t clip_start_y,
                                    int16_t clip_width, int16_t clip_height,
                                    const uint8_t *image_data) {
    // Bitmap data is packed RGB888 (3 bytes per pixel)
    for (int16_t row = 0; row < (int16_t)bitmap_height; row++) {
        int16_t sy = image_start_y + row;
        if (sy < clip_start_y || sy >= clip_start_y + clip_height) continue;

        for (int16_t col = 0; col < (int16_t)bitmap_width; col++) {
            int16_t sx = image_start_x + col;
            if (sx < clip_start_x || sx >= clip_start_x + clip_width) continue;

            const uint8_t *p = image_data + (row * bitmap_width + col) * 3;
            uint32_t rgb = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
            mw_hal_lcd_pixel(sx, sy, rgb);
        }
    }
}

#endif  // PURR_HAS_LVGL
