#pragma once
#include <stdint.h>
#include <lvgl.h>

#define TFT_BL      46
#define TFT_WIDTH  320
#define TFT_HEIGHT 480

void display_ili9488_init();
void display_ili9488_update();
void display_ili9488_deinit();
void display_ili9488_set_brightness(uint8_t level);
void display_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p);
