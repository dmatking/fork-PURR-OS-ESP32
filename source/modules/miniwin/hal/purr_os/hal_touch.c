// hal_touch.c — MiniWin HAL touch backend for PURR OS
//
// Routes touch queries through catcall_touch_t. No direct I2C/SPI here.

#include "hal/hal_touch.h"
#include "../../../../kernel/core/purr_kernel.h"

void mw_hal_touch_init(void)
{
    // Touch driver already initialised by driver_manager via catcall_touch_t.init()
    // Nothing to do here.
}

bool mw_hal_touch_is_recalibration_required(void)
{
    // Calibration check is not hardware-pin-based in the new architecture.
    // Return false — calibration is managed via settings NVS.
    return false;
}

mw_hal_touch_state_t mw_hal_touch_get_state(void)
{
    const catcall_touch_t *touch = purr_kernel_touch();
    if (!touch) return MW_HAL_TOUCH_STATE_UP;
    return touch->is_pressed() ? MW_HAL_TOUCH_STATE_DOWN : MW_HAL_TOUCH_STATE_UP;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y)
{
    const catcall_touch_t *touch = purr_kernel_touch();
    if (!touch) return false;

    int16_t tx = 0, ty = 0;
    if (!touch->read_point((uint16_t*)&tx, (uint16_t*)&ty)) return false;

    *x = (uint16_t)tx;
    *y = (uint16_t)ty;
    return true;
}
