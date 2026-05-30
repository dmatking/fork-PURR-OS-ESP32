#pragma once
#include <stdint.h>
#include <lvgl.h>

// CYD (ESP32-2432S028R) ILI9341 wiring — configure matching values in
// TFT_eSPI User_Setup.h:
//   #define ILI9341_DRIVER
//   #define TFT_MOSI  13
//   #define TFT_SCLK  14
//   #define TFT_MISO  12
//   #define TFT_CS    15
//   #define TFT_DC     2
//   #define TFT_RST   -1
//   #define LOAD_GLCD
//   #define SPI_FREQUENCY  40000000

#define CYD_TFT_BL     21
#define CYD_TFT_WIDTH  320
#define CYD_TFT_HEIGHT 240

void display_ili9341_init();
void display_ili9341_update();
void display_ili9341_deinit();
void display_ili9341_set_brightness(uint8_t level);
void display_ili9341_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p);
