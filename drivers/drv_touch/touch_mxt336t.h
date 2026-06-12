#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "purr_sys_drv.h"

// Atmel mXT336T I2C address and interrupt pin (adjust per schematic)
#define MXT_I2C_ADDR 0x4A
#define MXT_INT_PIN  2
#define MXT_SDA_PIN  21
#define MXT_SCL_PIN  22

typedef struct {
    uint16_t x;
    uint16_t y;
    bool     pressed;
    uint8_t  contact_id;
} mxt_touch_event_t;

void touch_mxt336t_init();
void touch_mxt336t_tick();
void touch_mxt336t_deinit();
void touch_mxt336t_drv_register(bool enabled);
bool touch_mxt336t_get_event(mxt_touch_event_t* out);
