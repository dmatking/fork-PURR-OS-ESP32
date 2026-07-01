#pragma once
#include "esp_err.h"

// Public surface used by specialized kernels that call the driver directly.
// Normal module path goes through purr_module_st7789.init(); this header is
// only needed when the specialized boot (kernel_tdp, kernel_td) inits the
// display before the module loader runs.

void st7789_configure(int cs, int dc, int mosi, int miso, int sclk, int rst, int bl);
int  st7789_drv_init(void);      // returns 0 on success, -1 on failure
void st7789_fill_screen(uint16_t color); // push uniform color to full GRAM (post-init)
