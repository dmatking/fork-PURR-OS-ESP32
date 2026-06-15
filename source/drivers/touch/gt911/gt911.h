#pragma once
#include "esp_err.h"

// Public surface for specialized kernels calling GT911 directly before the
// module loader runs.

int gt911_drv_init(void);   // uses default TDP pins (SDA=18, SCL=8, INT=16)
void gt911_drv_deinit(void);
