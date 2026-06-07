#pragma once
// touch_sim.h — routes touch_cst816s to app.h mouse state.

#include <stdint.h>
#include <stdbool.h>

struct cst_touch_event_t {
    bool    pressed;
    uint8_t gesture;
    int16_t x, y;
};

// Mouse state is provided by main.c and read here.
extern int  mx, my;
extern bool mouse_down;

static inline void touch_cst816s_init() {}
static inline bool touch_cst816s_get_event(cst_touch_event_t* ev) {
    ev->pressed = mouse_down;
    ev->gesture = 0;
    ev->x = (int16_t)mx;
    ev->y = (int16_t)my;
    return true;
}
