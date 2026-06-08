#pragma once
#include <stdint.h>

// ST7789 SPI display driver — Waveshare 1.69" (ESP32-S3, 240x280)
// WIP: verify pin assignments against hardware before use.
//
// Wiring (Waveshare ESP32-S3 1.69" default):
//   MOSI=13  SCLK=11  CS=12  DC=10  RST=14  BL=21

#define ST7789_TFT_BL     21
#define ST7789_TFT_WIDTH  240
#define ST7789_TFT_HEIGHT 280

void display_st7789_init();
void display_st7789_update();
void display_st7789_deinit();
void display_st7789_set_brightness(uint8_t level);

void display_st7789_clear();
void display_st7789_text(uint8_t row, const char* text);
void display_st7789_set_text_colors(uint16_t fg_rgb565, uint16_t bg_rgb565);

void display_st7789_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_st7789_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void display_st7789_draw_string(int16_t x, int16_t y, const char* s,
                                 uint16_t fg, uint16_t bg, uint8_t size);
// MiniWin HAL helpers — push_block = solid rect, push_colors = multi-color row burst
void display_st7789_push_block(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_st7789_push_colors(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* colors);
