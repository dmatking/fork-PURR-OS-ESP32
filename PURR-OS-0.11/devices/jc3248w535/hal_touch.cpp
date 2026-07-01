#include "hal/hal_touch.h"
#include "display_axs15231b.h"

extern "C" {

void mw_hal_touch_init(void) {
    display_axs15231b_touch_init();
}

bool mw_hal_touch_is_recalibration_required(void) {
    return false;  // AXS15231B integrated capacitive touch — no calibration
}

mw_hal_touch_state_t mw_hal_touch_get_state(void) {
    uint16_t x, y;
    return display_axs15231b_touch_read(&x, &y)
        ? MW_HAL_TOUCH_STATE_DOWN
        : MW_HAL_TOUCH_STATE_UP;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y) {
    uint16_t tx, ty;
    if (!display_axs15231b_touch_read(&tx, &ty)) return false;
    // Scale raw coordinates to MiniWin's 0..4095 range
    *x = (uint16_t)((uint32_t)tx * 4096 / AXS15231B_WIDTH);
    *y = (uint16_t)((uint32_t)ty * 4096 / AXS15231B_HEIGHT);
    return true;
}

} // extern "C"
