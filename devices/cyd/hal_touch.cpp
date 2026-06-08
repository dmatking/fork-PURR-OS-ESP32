#include "hal/hal_touch.h"
#include "display_ili9341.h"   // for CYD_TFT_WIDTH / CYD_TFT_HEIGHT
#include "touch_cst816s.h"

extern "C" {

static cst_touch_event_t s_ev = {};

void mw_hal_touch_init(void) {
    touch_cst816s_init();
}

bool mw_hal_touch_is_recalibration_required(void) {
    // CST816S is capacitive and self-calibrated — no resistive calibration screen needed
    return false;
}

mw_hal_touch_state_t mw_hal_touch_get_state(void) {
    touch_cst816s_get_event(&s_ev);
    return s_ev.pressed ? MW_HAL_TOUCH_STATE_DOWN : MW_HAL_TOUCH_STATE_UP;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y) {
    if (mw_hal_touch_get_state() == MW_HAL_TOUCH_STATE_UP) return false;
    *x = (uint16_t)((int32_t)s_ev.x * 4096 / CYD_TFT_WIDTH);
    *y = (uint16_t)((int32_t)s_ev.y * 4096 / CYD_TFT_HEIGHT);
    return true;
}

} // extern "C"
