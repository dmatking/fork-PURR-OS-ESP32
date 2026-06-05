#pragma once
#include <stdint.h>
#include <stdbool.h>

// GT911 capacitive I2C touch — JC3248W535 (ESP32-S3, 3.5" 480x320)
// WIP: verify pin assignments and I2C address against hardware.
//
// Wiring (JC3248W535 default):
//   SDA=19  SCL=20  INT=18  RST=38
//
// I2C address: 0x5D (INT pulled high at power-on)
//              0x14 (INT pulled low  at power-on)
// Most JC3248W535 boards use 0x5D — flip GT911_ADDR if touch is unresponsive.

struct gt911_touch_event_t {
    bool    pressed;
    uint8_t points;     // number of active touch points (1..5)
    int16_t x, y;       // first touch point in screen coordinates
};

void touch_gt911_init();
bool touch_gt911_get_event(gt911_touch_event_t* ev);
