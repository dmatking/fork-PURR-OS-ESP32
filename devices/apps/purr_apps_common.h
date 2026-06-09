#pragma once

// Shared WCE palette and window helpers for all purr_wm app windows.

#define WCE_BAR     0xC0C0C0u
#define WCE_HI      0xFFFFFFu
#define WCE_SHD     0x808080u
#define WCE_DARK    0x404040u
#define WCE_TXT     0x000000u

// Standard app window flags — fixed size (no resize/maximize), closeable, minimizable
#define APP_WIN_FLAGS \
    (MW_WINDOW_FLAG_HAS_TITLE_BAR | MW_WINDOW_FLAG_CAN_BE_CLOSED | \
     MW_WINDOW_FLAG_IS_VISIBLE    | MW_WINDOW_FLAG_FIXED_SIZE)

// Same but with touch events (for windows that need tap input)
#define APP_WIN_FLAGS_TOUCH \
    (MW_WINDOW_FLAG_HAS_TITLE_BAR | MW_WINDOW_FLAG_CAN_BE_CLOSED | \
     MW_WINDOW_FLAG_IS_VISIBLE    | MW_WINDOW_FLAG_FIXED_SIZE | \
     MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT)

// Horizontally center a window of width w on the display
#define APP_WIN_X(w) ((int16_t)((mw_hal_lcd_get_display_width() - (w)) / 2))
