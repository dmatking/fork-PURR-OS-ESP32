#pragma once
// IDF-only compatibility layer (Arduino removed as optional module).
// Keep stubs for removed Arduino-ESP32 functions.
#include "esp_log.h"
#include "driver/gpio.h"

// ledcSetup / ledcAttachPin were removed in Arduino-ESP32 3.x.
// Keep as no-ops for any call sites that haven't been updated.
#ifndef ledcSetup
static inline void ledcSetup(uint8_t, uint32_t, uint8_t) {}
#endif
#ifndef ledcAttachPin
static inline void ledcAttachPin(int, uint8_t) {}
#endif
