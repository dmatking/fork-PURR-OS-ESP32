// Generic MiniWin touch HAL stub — no touch driver wired up yet for this target.
// Replace with a real driver when adding touch support.

#include "hal/hal_touch.h"

extern "C" {

void mw_hal_touch_init(void) {}

bool mw_hal_touch_is_recalibration_required(void) {
    return false;
}

mw_hal_touch_state_t mw_hal_touch_get_state(void) {
    return MW_HAL_TOUCH_STATE_UP;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y) {
    (void)x; (void)y;
    return false;
}

} // extern "C"
