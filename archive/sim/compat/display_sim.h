#pragma once
// display_sim.h — routes display_ili9341_* to MiniWin's Windows HAL.
// Included instead of display_ili9341.h when compiling for the simulator.

#include "hal/hal_lcd.h"   // mw_hal_lcd_colour_t, mw_hal_lcd_filled_rectangle
#include <stdint.h>

#define CYD_TFT_BL     27   // unused in sim
#ifdef SIM_SHELL_WIDTH
#  define CYD_TFT_WIDTH  SIM_SHELL_WIDTH
#  define CYD_TFT_HEIGHT SIM_SHELL_HEIGHT
#else
#  define CYD_TFT_WIDTH  320
#  define CYD_TFT_HEIGHT 240
#endif

static inline mw_hal_lcd_colour_t _rgb565_to_mw(uint16_t c) {
    uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
    uint8_t g = (uint8_t)(((c >>  5) & 0x3F) << 2);
    uint8_t b = (uint8_t)(((c      ) & 0x1F) << 3);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static inline void display_ili9341_init()                                     {}
static inline void display_ili9341_update()                                   {}
static inline void display_ili9341_deinit()                                   {}
static inline void display_ili9341_set_brightness(uint8_t)                    {}
static inline void display_ili9341_clear()                                    {
    mw_hal_lcd_filled_rectangle(0, 0, CYD_TFT_WIDTH, CYD_TFT_HEIGHT, 0x000000);
}
static inline void display_ili9341_text(uint8_t, const char*)                 {}
static inline void display_ili9341_set_text_colors(uint16_t, uint16_t)        {}
static inline void display_ili9341_fill_rect(int16_t x, int16_t y,
                                              int16_t w, int16_t h,
                                              uint16_t rgb565) {
    mw_hal_lcd_filled_rectangle(x, y, w, h, _rgb565_to_mw(rgb565));
}
static inline void display_ili9341_draw_hline(int16_t x, int16_t y,
                                               int16_t w, uint16_t rgb565) {
    mw_hal_lcd_filled_rectangle(x, y, w, 1, _rgb565_to_mw(rgb565));
}
static inline void display_ili9341_draw_string(int16_t, int16_t, const char*,
                                                uint16_t, uint16_t, uint8_t)  {}
