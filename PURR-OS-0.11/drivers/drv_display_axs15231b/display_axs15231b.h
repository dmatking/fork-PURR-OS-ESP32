#pragma once
#include <stdint.h>
#include "purr_sys_drv.h"

// AXS15231B QSPI display + integrated touch driver
// Target: JC3248W535 3.5" 320x480 portrait
// Wiring (verified from community driver):
//   QSPI: PCLK=47  D0=21  D1=48  D2=40  D3=39  CS=45  RST=-1(tied hi)  BL=1
//   Touch I2C: SDA=4  SCL=8

#define AXS15231B_WIDTH   320
#define AXS15231B_HEIGHT  480
#define AXS15231B_BL_PIN  1

void display_axs15231b_init();
void display_axs15231b_tick();
void display_axs15231b_deinit();
void display_axs15231b_drv_register(bool enabled);
void display_axs15231b_set_brightness(uint8_t level);

void display_axs15231b_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_axs15231b_draw_string(int16_t x, int16_t y, const char* s,
                                    uint16_t fg, uint16_t bg, uint8_t size);
void display_axs15231b_push_block(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_axs15231b_push_colors(int16_t x, int16_t y, int16_t w, int16_t h,
                                    const uint16_t* colors);

// Touch — called from hal_touch.cpp
bool display_axs15231b_touch_init();
bool display_axs15231b_touch_read(uint16_t *x, uint16_t *y);
