#include "hal/hal_touch.h"
#include "display_st7796.h"    // for ST7796_TFT_WIDTH / ST7796_TFT_HEIGHT
#include "touch_gt911.h"

extern "C" {

static gt911_touch_event_t s_ev = {};

void mw_hal_touch_init(void) {
    touch_gt911_init();
}

bool mw_hal_touch_is_recalibration_required(void) {
    // GT911 is capacitive — no resistive calibration needed
    return false;
}

mw_hal_touch_state_t mw_hal_touch_get_state(void) {
    touch_gt911_get_event(&s_ev);
    return s_ev.pressed ? MW_HAL_TOUCH_STATE_DOWN : MW_HAL_TOUCH_STATE_UP;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y) {
    if (mw_hal_touch_get_state() == MW_HAL_TOUCH_STATE_UP) return false;
    *x = (uint16_t)((int32_t)s_ev.x * 4096 / ST7796_TFT_WIDTH);
    *y = (uint16_t)((int32_t)s_ev.y * 4096 / ST7796_TFT_HEIGHT);
    return true;
}

} // extern "C"
