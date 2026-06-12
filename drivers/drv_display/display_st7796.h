#pragma once
#include <stdint.h>
#include "purr_sys_drv.h"

// ST7796 SPI display driver — JC3248W535 (ESP32-S3, 3.5" 480x320)
// WIP: verify pin assignments against hardware before use.
//
// Wiring (JC3248W535 default):
//   MOSI=13  SCLK=12  CS=10  DC=11  RST=-1(tied hi)  BL=27

#define ST7796_TFT_BL     27
#define ST7796_TFT_WIDTH  480
#define ST7796_TFT_HEIGHT 320

void display_st7796_init();
void display_st7796_tick();
void display_st7796_deinit();
void display_st7796_drv_register(bool enabled);
void display_st7796_set_brightness(uint8_t level);

void display_st7796_clear();
void display_st7796_text(uint8_t row, const char* text);
void display_st7796_set_text_colors(uint16_t fg_rgb565, uint16_t bg_rgb565);

void display_st7796_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_st7796_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void display_st7796_draw_string(int16_t x, int16_t y, const char* s,
                                 uint16_t fg, uint16_t bg, uint8_t size);
// MiniWin HAL helpers — push_block = solid rect, push_colors = multi-color row burst
void display_st7796_push_block(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_st7796_push_colors(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* colors);
