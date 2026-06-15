#pragma once
#include "esp_err.h"

// Public surface for specialized kernels calling GT911 directly before the
// module loader runs.

// Optional: call before gt911_drv_init() to override pin assignments.
// Pass int_pin=-1 to use polling mode instead of GPIO interrupt.
void gt911_configure(int sda, int scl, int int_pin, int rst_pin, int i2c_port);

int gt911_drv_init(void);   // uses default TDP pins (SDA=18, SCL=8, INT=16)
void gt911_drv_deinit(void);
