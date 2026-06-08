#pragma once
// Arduino-ESP32 now provides the full Arduino API.
// This file is kept so existing #include "purr_idf_compat.h" sites compile unchanged.
#include <Arduino.h>
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
