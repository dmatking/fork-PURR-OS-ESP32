#pragma once
#include "device_config.h"

// Register all compile-time drivers into the sys_drv registry.
// Call before sys_drv_init_all().
void purr_drv_register_all(const device_config_t *cfg);
