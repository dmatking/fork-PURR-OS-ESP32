// hal_touch.c — MiniWin HAL touch backend for PURR OS
//
// Merges real touch (catcall_touch_t) with cursor-click (trackball button).
// Real touch hides the cursor; trackball click synthesizes a touch at the
// cursor position so MiniWin responds as if the user tapped there.

#include "hal/hal_touch.h"
#include "hal/hal_lcd.h"
#include "../../../../kernel/core/purr_kernel.h"
#include "../../miniwin_cursor.h"

void mw_hal_touch_init(void)
{
    // Touch driver already initialised before modules load via baked-in init.
}

bool mw_hal_touch_is_recalibration_required(void)
{
    return false;
}

mw_hal_touch_state_t mw_hal_touch_get_state(void)
{
    const catcall_touch_t *touch = purr_kernel_touch();
    bool real = touch && touch->is_pressed();
    bool cursor_click = miniwin_cursor_pressed();
    return (real || cursor_click) ? MW_HAL_TOUCH_STATE_DOWN : MW_HAL_TOUCH_STATE_UP;
}

bool mw_hal_touch_get_point(uint16_t *x, uint16_t *y)
{
    // Cursor click takes priority when no real touch is active
    const catcall_touch_t *touch = purr_kernel_touch();
    bool real = touch && touch->is_pressed();

    if (!real && miniwin_cursor_pressed()) {
        *x = miniwin_cursor_x();
        *y = miniwin_cursor_y();
        return true;
    }

    if (!touch) return false;
    uint16_t tx = 0, ty = 0;
    if (!touch->read_point(&tx, &ty)) return false;

#ifdef CONFIG_PURR_TOUCH_FLIP_X
    {
        int16_t w = mw_hal_lcd_get_display_width();
        tx = (uint16_t)(tx < (uint16_t)w ? (uint16_t)(w - 1) - tx : 0);
    }
#endif
#ifdef CONFIG_PURR_TOUCH_FLIP_Y
    {
        int16_t h = mw_hal_lcd_get_display_height();
        ty = (uint16_t)(ty < (uint16_t)h ? (uint16_t)(h - 1) - ty : 0);
    }
#endif

    *x = tx;
    *y = ty;
    return true;
}
