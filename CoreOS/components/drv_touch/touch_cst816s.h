#pragma once
#include <stdint.h>
#include <stdbool.h>

// CST816S capacitive I2C touch — CYD ESP32-2432S024C
// SDA=33, SCL=32, INT=21, RST=25, addr=0x15

struct cst_touch_event_t {
    bool    pressed;
    uint8_t gesture;   // 0x00=none, 0x01=swipe up, 0x02=dn, 0x03=left, 0x04=right, 0x05=tap
    int16_t x, y;      // landscape screen coordinates (0..319, 0..239)
};

void touch_cst816s_init();
bool touch_cst816s_get_event(cst_touch_event_t* ev);
