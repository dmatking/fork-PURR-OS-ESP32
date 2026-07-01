#pragma once
#include "../device_config.h"

void flasher_init();
void flasher_update();
void flasher_deinit();

// Entry point — takes over from KITT::init() if boot flag is set.
// Never returns; calls esp_restart() after flashing or timeout.
void flasher_run(device_config_t* cfg);
