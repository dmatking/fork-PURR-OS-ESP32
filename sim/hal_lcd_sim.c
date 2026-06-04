// hal_lcd_sim.c — MiniWin Windows HAL for the PURR OS simulator.
// Identical to MiniWin's upstream windows/hal_lcd.c but with 320x240 (landscape)
// instead of the default 240x320 (portrait). The window is created by main.c.

#ifdef _WIN32

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include "hal/hal_lcd.h"
#include "app.h"

// 320x240 landscape to match the CYD ILI9341
#define LCD_DISPLAY_WIDTH_PIXELS    320
#define LCD_DISPLAY_HEIGHT_PIXELS   240

static HDC hdc;

void mw_hal_lcd_init(void)
{
    Sleep(200U);
}

int16_t mw_hal_lcd_get_display_width(void)
{
    return LCD_DISPLAY_WIDTH_PIXELS;
}

int16_t mw_hal_lcd_get_display_height(void)
{
    return LCD_DISPLAY_HEIGHT_PIXELS;
}

void mw_hal_lcd_pixel(int16_t x, int16_t y, mw_hal_lcd_colour_t colour)
{
    hdc = GetDC(hwnd);
    uint8_t r = (uint8_t)((colour & 0xff0000U) >> 16);
    uint8_t g = (uint8_t)((colour & 0x00ff00U) >> 8);
    uint8_t b = (uint8_t) (colour & 0x0000ffU);
    (void)SetPixel(hdc, x, y, RGB(r, g, b));
    (void)ReleaseDC(hwnd, hdc);
}

void mw_hal_lcd_filled_rectangle(int16_t start_x, int16_t start_y,
                                  int16_t width,   int16_t height,
                                  mw_hal_lcd_colour_t colour)
{
    HBRUSH brush;
    RECT   rect;
    uint8_t r = (uint8_t)((colour & 0xff0000U) >> 16U);
    uint8_t g = (uint8_t)((colour & 0x00ff00U) >> 8U);
    uint8_t b = (uint8_t) (colour & 0x0000ffU);

    hdc   = GetDC(hwnd);
    brush = CreateSolidBrush(RGB(r, g, b));
    rect.left   = start_x;
    rect.top    = start_y;
    rect.right  = start_x + width;
    rect.bottom = start_y + height;
    (void)FillRect(hdc, &rect, brush);
    (void)DeleteObject(brush);
    (void)ReleaseDC(hwnd, hdc);
}

void mw_hal_lcd_colour_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                    uint16_t bitmap_width, uint16_t bitmap_height,
                                    int16_t clip_start_x,  int16_t clip_start_y,
                                    int16_t clip_width,    int16_t clip_height,
                                    const uint8_t *image_data)
{
    hdc = GetDC(hwnd);
    for (int16_t y = 0; y < (int16_t)bitmap_height; y++) {
        for (int16_t x = 0; x < (int16_t)bitmap_width; x++) {
            if (x + image_start_x >= clip_start_x &&
                x + image_start_x <  clip_start_x + clip_width &&
                y + image_start_y >= clip_start_y &&
                y + image_start_y <  clip_start_y + clip_height) {
                const uint8_t *p = image_data + (x + y * bitmap_width) * 3;
                (void)SetPixel(hdc, x + image_start_x, y + image_start_y,
                               RGB(p[2], p[1], p[0]));
            }
        }
    }
    (void)ReleaseDC(hwnd, hdc);
}

void mw_hal_lcd_monochrome_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                        uint16_t bitmap_width, uint16_t bitmap_height,
                                        int16_t clip_start_x,  int16_t clip_start_y,
                                        int16_t clip_width,    int16_t clip_height,
                                        mw_hal_lcd_colour_t fg, mw_hal_lcd_colour_t bg,
                                        const uint8_t *image_data)
{
    uint8_t fr = (uint8_t)((fg & 0xff0000U) >> 16U);
    uint8_t fg_ = (uint8_t)((fg & 0x00ff00U) >> 8U);
    uint8_t fb = (uint8_t) (fg & 0x0000ffU);
    uint8_t br = (uint8_t)((bg & 0xff0000U) >> 16U);
    uint8_t bg_ = (uint8_t)((bg & 0x00ff00U) >> 8U);
    uint8_t bb = (uint8_t) (bg & 0x0000ffU);

    hdc = GetDC(hwnd);
    for (int16_t y = 0; y < (int16_t)bitmap_height; y++) {
        for (int16_t x = 0; x < (int16_t)bitmap_width; x++) {
            if (x + image_start_x >= clip_start_x &&
                x + image_start_x <  clip_start_x + clip_width &&
                y + image_start_y >= clip_start_y &&
                y + image_start_y <  clip_start_y + clip_height) {
                uint8_t byte = image_data[(x + y * bitmap_width) / 8];
                int bit = 7 - ((x + y * bitmap_width) % 8);
                COLORREF c = ((byte >> bit) & 1)
                    ? RGB(fr, fg_, fb)
                    : RGB(br, bg_, bb);
                (void)SetPixel(hdc, x + image_start_x, y + image_start_y, c);
            }
        }
    }
    (void)ReleaseDC(hwnd, hdc);
}

#endif // _WIN32
