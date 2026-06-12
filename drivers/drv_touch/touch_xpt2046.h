#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "purr_sys_drv.h"

// CYD XPT2046 wiring — uses software SPI to avoid bus conflict with display HSPI
//   T_MOSI = 32   (output)
//   T_MISO = 39   (input-only GPIO)
//   T_SCLK = 25   (output)
//   T_CS   = 33   (output)
//   T_IRQ  = 36   (input-only GPIO)

typedef struct {
    uint16_t x;
    uint16_t y;
    bool     pressed;
} xpt_touch_event_t;

void touch_xpt2046_init();
void touch_xpt2046_deinit();
void touch_xpt2046_drv_register(bool enabled);
bool touch_xpt2046_get_event(xpt_touch_event_t* out);

#ifdef PURR_HAS_LVGL
#include <lvgl.h>
void touch_xpt2046_lvgl_read(lv_indev_drv_t* drv, lv_indev_data_t* data);
#endif
