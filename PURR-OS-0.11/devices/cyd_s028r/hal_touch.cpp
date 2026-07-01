#include "hal/hal_touch.h"
#include "display_ili9341.h"   // for CYD_TFT_WIDTH / CYD_TFT_HEIGHT
#include "touch_xpt2046.h"

extern "C" {

static xpt_touch_event_t s_ev = {};

void mw_hal_touch_init(void) {
    touch_xpt2046_init();
}

bool mw_hal_touch_is_recalibration_required(void) {
    // XPT2046 is resistive — MiniWin will show calibration screen on first boot
    // if NVS calibration data is absent.
    return false;
}

mw_hal_touch_state_t mw_hal_touch_get_state(void) {
    touch_xpt2046_get_event(&s_ev);
    return s_ev.pressed ? MW_HAL_TOUCH_STATE_DOWN : MW_HAL_TOUCH_STATE_UP;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y) {
    if (mw_hal_touch_get_state() == MW_HAL_TOUCH_STATE_UP) return false;
    // xpt2046 returns screen coords 0-319 / 0-239; scale to 0-4095 for MiniWin
    *x = (uint16_t)((int32_t)s_ev.x * 4096 / CYD_TFT_WIDTH);
    *y = (uint16_t)((int32_t)s_ev.y * 4096 / CYD_TFT_HEIGHT);
    return true;
}

} // extern "C"
