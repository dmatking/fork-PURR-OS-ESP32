#pragma once
// display_ili9341.h — ILI9341 display driver (pure ESP-IDF, no Arduino)
#include "purr_sys_drv.h"
// ESP32-2432S028R: HSPI MOSI=13 MISO=12 SCLK=14 CS=15 DC=2 RST=4  BL=21
// ESP32-2432S024C: same bus, BL=27, RST=-1 (board reset)

#include <stdint.h>

#define CYD_TFT_WIDTH  320
#define CYD_TFT_HEIGHT 240

void display_ili9341_init(void);
void display_ili9341_tick(void);
void display_ili9341_deinit(void);
void display_ili9341_drv_register(bool enabled);
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
