#pragma once
#include <stdint.h>

// CYD (ESP32-2432S028R) ILI9341 wiring
// Backlight pin differs by CYD variant:
//   S028R (2.8" resistive) = 21
//   S024C (2.4" capacitive) = 27
#ifdef CYD_VARIANT_S024C
#  define CYD_TFT_BL  27
#else
#  define CYD_TFT_BL  21
#endif
#define CYD_TFT_WIDTH  320
#define CYD_TFT_HEIGHT 240

void display_ili9341_init();
void display_ili9341_update();
void display_ili9341_deinit();
void display_ili9341_set_brightness(uint8_t level);

// Row-based text output (boot splash / emergency / verbose log)
void display_ili9341_clear();
void display_ili9341_text(uint8_t row, const char* text);
void display_ili9341_set_text_colors(uint16_t fg_rgb565, uint16_t bg_rgb565);

// Drawing primitives (used by purr_shell and any future TFT-direct UI)
void display_ili9341_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_ili9341_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void display_ili9341_draw_string(int16_t x, int16_t y, const char* s,
                                  uint16_t fg, uint16_t bg, uint8_t size);

#ifdef PURR_HAS_LVGL
#include <lvgl.h>
void display_ili9341_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p);
#endif
