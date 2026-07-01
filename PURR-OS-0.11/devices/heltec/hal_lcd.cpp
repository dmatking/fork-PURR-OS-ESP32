// Generic MiniWin LCD HAL stub — no display driver wired up yet for this target.
// Replace with a real driver when adding display support.

#include "hal/hal_lcd.h"

extern "C" {

void mw_hal_lcd_init(void) {}

int16_t mw_hal_lcd_get_display_width(void)  { return 320; }
int16_t mw_hal_lcd_get_display_height(void) { return 240; }

void mw_hal_lcd_pixel(int16_t x, int16_t y, mw_hal_lcd_colour_t colour) {
    (void)x; (void)y; (void)colour;
}

void mw_hal_lcd_filled_rectangle(int16_t start_x, int16_t start_y,
                                  int16_t width, int16_t height,
                                  mw_hal_lcd_colour_t colour) {
    (void)start_x; (void)start_y; (void)width; (void)height; (void)colour;
}

void mw_hal_lcd_monochrome_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                        uint16_t bitmap_width, uint16_t bitmap_height,
                                        int16_t clip_start_x, int16_t clip_start_y,
                                        int16_t clip_width, int16_t clip_height,
                                        mw_hal_lcd_colour_t fg_colour,
                                        mw_hal_lcd_colour_t bg_colour,
                                        const uint8_t *image_data) {
    (void)image_start_x; (void)image_start_y;
    (void)bitmap_width;  (void)bitmap_height;
    (void)clip_start_x;  (void)clip_start_y;
    (void)clip_width;    (void)clip_height;
    (void)fg_colour;     (void)bg_colour;
    (void)image_data;
}

void mw_hal_lcd_colour_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                    uint16_t bitmap_width, uint16_t bitmap_height,
                                    int16_t clip_start_x, int16_t clip_start_y,
                                    int16_t clip_width, int16_t clip_height,
                                    const uint8_t *image_data) {
    (void)image_start_x; (void)image_start_y;
    (void)bitmap_width;  (void)bitmap_height;
    (void)clip_start_x;  (void)clip_start_y;
    (void)clip_width;    (void)clip_height;
    (void)image_data;
}

} // extern "C"
