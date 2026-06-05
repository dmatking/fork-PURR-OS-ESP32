#ifdef PURR_CYD

#include "hal/hal_lcd.h"
#include "display_ili9341.h"

extern "C" {

// MiniWin colours are 0x00RRGGBB; ILI9341 uses RGB565
static inline uint16_t to_rgb565(mw_hal_lcd_colour_t c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >>  8) & 0xFF;
    uint8_t b = (c      ) & 0xFF;
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

void mw_hal_lcd_init(void) {
    display_ili9341_init();
}

int16_t mw_hal_lcd_get_display_width(void)  { return CYD_TFT_WIDTH;  }
int16_t mw_hal_lcd_get_display_height(void) { return CYD_TFT_HEIGHT; }

void mw_hal_lcd_pixel(int16_t x, int16_t y, mw_hal_lcd_colour_t colour) {
    display_ili9341_fill_rect(x, y, 1, 1, to_rgb565(colour));
}

void mw_hal_lcd_filled_rectangle(int16_t start_x, int16_t start_y,
                                  int16_t width, int16_t height,
                                  mw_hal_lcd_colour_t colour) {
    display_ili9341_fill_rect(start_x, start_y, width, height, to_rgb565(colour));
}

void mw_hal_lcd_monochrome_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                        uint16_t bitmap_width, uint16_t bitmap_height,
                                        int16_t clip_start_x, int16_t clip_start_y,
                                        int16_t clip_width, int16_t clip_height,
                                        mw_hal_lcd_colour_t fg_colour,
                                        mw_hal_lcd_colour_t bg_colour,
                                        const uint8_t *image_data) {
    uint16_t fg565 = to_rgb565(fg_colour);
    uint16_t bg565 = to_rgb565(bg_colour);
    uint16_t stride = (bitmap_width + 7) / 8;

    for (int16_t row = 0; row < (int16_t)bitmap_height; row++) {
        int16_t sy = image_start_y + row;
        if (sy < clip_start_y || sy >= clip_start_y + clip_height) continue;
        for (int16_t col = 0; col < (int16_t)bitmap_width; col++) {
            int16_t sx = image_start_x + col;
            if (sx < clip_start_x || sx >= clip_start_x + clip_width) continue;
            bool set = (image_data[row * stride + col / 8] >> (7 - (col & 7))) & 1;
            display_ili9341_fill_rect(sx, sy, 1, 1, set ? fg565 : bg565);
        }
    }
}

void mw_hal_lcd_colour_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                    uint16_t bitmap_width, uint16_t bitmap_height,
                                    int16_t clip_start_x, int16_t clip_start_y,
                                    int16_t clip_width, int16_t clip_height,
                                    const uint8_t *image_data) {
    // Bitmap data is packed RGB888 (3 bytes per pixel, row-major)
    for (int16_t row = 0; row < (int16_t)bitmap_height; row++) {
        int16_t sy = image_start_y + row;
        if (sy < clip_start_y || sy >= clip_start_y + clip_height) continue;
        for (int16_t col = 0; col < (int16_t)bitmap_width; col++) {
            int16_t sx = image_start_x + col;
            if (sx < clip_start_x || sx >= clip_start_x + clip_width) continue;
            const uint8_t *p = image_data + (row * bitmap_width + col) * 3;
            uint16_t rgb565 = ((uint16_t)(p[0] & 0xF8) << 8)
                            | ((uint16_t)(p[1] & 0xFC) << 3)
                            | (p[2] >> 3);
            display_ili9341_fill_rect(sx, sy, 1, 1, rgb565);
        }
    }
}

} // extern "C"
#endif // PURR_CYD
