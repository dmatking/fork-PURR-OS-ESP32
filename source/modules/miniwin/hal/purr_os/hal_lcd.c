// hal_lcd.c — MiniWin HAL LCD backend for PURR OS
//
// Routes all display calls through the catcall_display_t API registered
// in the kernel. No direct SPI / GPIO knowledge here.

#include "hal/hal_lcd.h"
#include "../../../../kernel/core/purr_kernel.h"
#include <string.h>
#include <stdint.h>

// Line buffer for row-at-a-time push_pixels calls.
// 480px * 2 bytes covers the widest display we ship on.
static uint16_t s_line_buf[480];

static int16_t s_width  = 240;
static int16_t s_height = 320;

// rgb888 → rgb565 (big-endian, matching catcall_display_t expectation)
static inline uint16_t rgb888_to_rgb565(uint32_t c)
{
    uint16_t r = (c >> 16) & 0xF8u;
    uint16_t g = (c >>  8) & 0xFCu;
    uint16_t b = (c      ) & 0xF8u;
    return __builtin_bswap16((uint16_t)((r << 8) | (g << 3) | (b >> 3)));
}

void mw_hal_lcd_init(void)
{
    const catcall_display_t *disp = purr_kernel_display();
    if (!disp) return;
    display_info_t info = {0};
    disp->get_info(&info);
    s_width  = (int16_t)info.width;
    s_height = (int16_t)info.height;
}

int16_t mw_hal_lcd_get_display_width(void)  { return s_width;  }
int16_t mw_hal_lcd_get_display_height(void) { return s_height; }

void mw_hal_lcd_pixel(int16_t x, int16_t y, mw_hal_lcd_colour_t colour)
{
    const catcall_display_t *disp = purr_kernel_display();
    if (!disp) return;
    uint16_t px = rgb888_to_rgb565((uint32_t)colour);
    disp->push_pixels(x, y, 1, 1, &px);
}

void mw_hal_lcd_filled_rectangle(int16_t start_x, int16_t start_y,
                                  int16_t width, int16_t height,
                                  mw_hal_lcd_colour_t colour)
{
    const catcall_display_t *disp = purr_kernel_display();
    if (!disp) return;
    disp->fill_rect(start_x, start_y, width, height,
                    rgb888_to_rgb565((uint32_t)colour));
}

void mw_hal_lcd_colour_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                    uint16_t bitmap_width, uint16_t bitmap_height,
                                    int16_t clip_start_x, int16_t clip_start_y,
                                    int16_t clip_width, int16_t clip_height,
                                    const uint8_t *image_data)
{
    const catcall_display_t *disp = purr_kernel_display();
    if (!disp) return;

    for (int16_t y = 0; y < (int16_t)bitmap_height; y++) {
        int16_t screen_y = image_start_y + y;
        if (screen_y < clip_start_y || screen_y >= clip_start_y + clip_height) continue;

        int16_t row_start = -1;
        int16_t row_len   = 0;

        for (int16_t x = 0; x < (int16_t)bitmap_width; x++) {
            int16_t screen_x = image_start_x + x;
            if (screen_x < clip_start_x || screen_x >= clip_start_x + clip_width) continue;

            const uint8_t *px_src = image_data + (y * (int16_t)bitmap_width + x) * 3;
            uint32_t rgb888 = ((uint32_t)px_src[2] << 16) |
                              ((uint32_t)px_src[1] <<  8) |
                               (uint32_t)px_src[0];

            if (row_start < 0) row_start = screen_x;
            s_line_buf[row_len++] = rgb888_to_rgb565(rgb888);
        }

        if (row_len > 0) {
            disp->push_pixels(row_start, screen_y, row_len, 1, s_line_buf);
        }
    }
}

void mw_hal_lcd_monochrome_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                        uint16_t bitmap_width, uint16_t bitmap_height,
                                        int16_t clip_start_x, int16_t clip_start_y,
                                        int16_t clip_width, int16_t clip_height,
                                        mw_hal_lcd_colour_t fg_colour,
                                        mw_hal_lcd_colour_t bg_colour,
                                        const uint8_t *image_data)
{
    const catcall_display_t *disp = purr_kernel_display();
    if (!disp) return;

    uint16_t fg565 = rgb888_to_rgb565((uint32_t)fg_colour);
    uint16_t bg565 = rgb888_to_rgb565((uint32_t)bg_colour);
    int16_t  row_bytes = ((int16_t)bitmap_width + 7) / 8;

    for (int16_t y = 0; y < (int16_t)bitmap_height; y++) {
        int16_t screen_y = image_start_y + y;
        if (screen_y < clip_start_y || screen_y >= clip_start_y + clip_height) continue;

        int16_t row_start = -1;
        int16_t row_len   = 0;

        for (int16_t x = 0; x < (int16_t)bitmap_width; x++) {
            int16_t screen_x = image_start_x + x;
            if (screen_x < clip_start_x || screen_x >= clip_start_x + clip_width) continue;

            uint8_t byte = image_data[y * row_bytes + x / 8];
            uint8_t bit  = (byte >> (7 - (x % 8))) & 1u;

            if (row_start < 0) row_start = screen_x;
            s_line_buf[row_len++] = bit ? fg565 : bg565;
        }

        if (row_len > 0) {
            disp->push_pixels(row_start, screen_y, row_len, 1, s_line_buf);
        }
    }
}
