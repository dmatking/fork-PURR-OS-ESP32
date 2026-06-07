#pragma once
#include <stdint.h>
#include <stdbool.h>

// Atmel mXT336T I2C address and interrupt pin (adjust per schematic)
#define MXT_I2C_ADDR 0x4A
#define MXT_INT_PIN   2

typedef struct {
    uint16_t x;
    uint16_t y;
    bool     pressed;
    uint8_t  contact_id;
} mxt_touch_event_t;

void touch_mxt336t_init();
void touch_mxt336t_update();
void touch_mxt336t_deinit();
bool touch_mxt336t_get_event(mxt_touch_event_t* out);
