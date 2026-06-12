#pragma once
#include <stdint.h>
#include "purr_sys_drv.h"

#define OLED_SDA      17
#define OLED_SCL      18
#define OLED_RST      21
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64

void display_ssd1306_init();
void display_ssd1306_tick();
void display_ssd1306_deinit();
void display_ssd1306_drv_register(bool enabled);

void display_ssd1306_text(uint8_t row, const char* text);
void display_ssd1306_clear();
void display_ssd1306_set_cursor(uint8_t x, uint8_t y);
void display_ssd1306_print(const char* text);
