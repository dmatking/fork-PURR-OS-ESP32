#pragma once
// display_ili9341.h — ILI9341 display driver (pure ESP-IDF)

#include <stdint.h>

// ESP32-2432S028R and S024C share the same HSPI display bus.
// S024C: RST is tied to board reset (no dedicated GPIO), BL=27, CTP_INT=21 (separate).
// S028R: RST=4 (dedicated GPIO).
#ifdef CYD_VARIANT_S024C
#  define CYD_TFT_BL   27
#  define CYD_TFT_MOSI 13
#  define CYD_TFT_MISO 12
#  define CYD_TFT_SCK  14
#  define CYD_TFT_CS   15
#  define CYD_TFT_DC   2
#  define CYD_TFT_RST  (-1)
#else
// ESP32-2432S028R
#  define CYD_TFT_BL   21
#  define CYD_TFT_MOSI 13
#  define CYD_TFT_MISO 12
#  define CYD_TFT_SCK  14
#  define CYD_TFT_CS   15
#  define CYD_TFT_DC   2
#  define CYD_TFT_RST  4
#endif
#define CYD_TFT_WIDTH  320
#define CYD_TFT_HEIGHT 240

void display_ili9341_init(void);
void display_ili9341_update(void);
void display_ili9341_deinit(void);
void display_ili9341_set_brightness(uint8_t level);

void display_ili9341_clear(void);
void display_ili9341_text(uint8_t row, const char* text);
void display_ili9341_set_text_colors(uint16_t fg_rgb565, uint16_t bg_rgb565);

void display_ili9341_push_block(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_ili9341_push_colors(int16_t x, int16_t y, int16_t w, int16_t h,
                                  const uint16_t* colors);
void display_ili9341_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_ili9341_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void display_ili9341_draw_string(int16_t x, int16_t y, const char* s,
                                  uint16_t fg, uint16_t bg, uint8_t size);

#ifdef PURR_HAS_LVGL
#include <lvgl.h>
void display_ili9341_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p);
#endif
