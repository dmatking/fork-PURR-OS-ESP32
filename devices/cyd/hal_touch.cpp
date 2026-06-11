#include "hal/hal_touch.h"
#include "display_ili9341.h"   // for CYD_TFT_WIDTH / CYD_TFT_HEIGHT

#if defined(PURR_HAS_CST816S_TOUCH)
#include "touch_cst816s.h"
static cst_touch_event_t s_ev = {};
#else
#include "touch_xpt2046.h"
static xpt_touch_event_t s_ev = {};
#endif

extern "C" {

void mw_hal_touch_init(void) {
#if defined(PURR_HAS_CST816S_TOUCH)
    touch_cst816s_init();
#else
    touch_xpt2046_init();
#endif
}

bool mw_hal_touch_is_recalibration_required(void) {
    return false;
}

mw_hal_touch_state_t mw_hal_touch_get_state(void) {
#if defined(PURR_HAS_CST816S_TOUCH)
    touch_cst816s_get_event(&s_ev);
#else
    touch_xpt2046_get_event(&s_ev);
#endif
    return s_ev.pressed ? MW_HAL_TOUCH_STATE_DOWN : MW_HAL_TOUCH_STATE_UP;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y) {
    if (mw_hal_touch_get_state() == MW_HAL_TOUCH_STATE_UP) return false;
    *x = (uint16_t)((int32_t)s_ev.x * 4096 / CYD_TFT_WIDTH);
    *y = (uint16_t)((int32_t)s_ev.y * 4096 / CYD_TFT_HEIGHT);
    return true;
}

} // extern "C"
