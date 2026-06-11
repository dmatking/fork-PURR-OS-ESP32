#pragma once

// PURR OS — MiniWin configuration for Waveshare 1.69" ESP32-S3
// Display: ST7789 240x280 (portrait-native)
// Touch:   CST816S I2C — verify pins against hardware before use
//          Common wiring: SDA=6  SCL=7  INT=9  RST=8

#ifdef __cplusplus
extern "C" {
#endif

#define MW_MAX_WINDOW_COUNT         14U
#define MW_MAX_CONTROL_COUNT        24U
#define MW_MAX_TIMER_COUNT          8U
#define MW_MESSAGE_QUEUE_SIZE       100U

// Display is portrait 240x280 — root window will be 240 wide, 280 tall.
// Landscape layout will be cramped; a portrait-optimised purr_app.cpp is recommended.
#define MW_DISPLAY_ROTATION_0

#if !defined(MW_DISPLAY_ROTATION_0) && !defined(MW_DISPLAY_ROTATION_90) \
 && !defined(MW_DISPLAY_ROTATION_180) && !defined(MW_DISPLAY_ROTATION_270)
#error Display rotation not defined
#endif

#if defined(MW_DISPLAY_ROTATION_0) || defined(MW_DISPLAY_ROTATION_180)
#define MW_ROOT_WIDTH               mw_hal_lcd_get_display_width()
#define MW_ROOT_HEIGHT              mw_hal_lcd_get_display_height()
#elif defined(MW_DISPLAY_ROTATION_90) || defined(MW_DISPLAY_ROTATION_270)
#define MW_ROOT_WIDTH               mw_hal_lcd_get_display_height()
#define MW_ROOT_HEIGHT              mw_hal_lcd_get_display_width()
#endif

#define MW_MAX_TITLE_SIZE           14U

#include "../purr_miniwin_colors.h"

#define MW_TICK_PERIOD_MS               20
#define MW_TICKS_PER_SECOND             50U
#define MW_CONTROL_DOWN_TIME            4U
#define MW_KEY_DOWN_TIME                3U
#define MW_WINDOW_MIN_MAX_EFFECT_TIME   5U
#define MW_CURSOR_PERIOD_TICKS          10U
#define MW_TOUCH_INTERVAL_TICKS         2U
#define MW_HOLD_DOWN_DELAY_TICKS        10U

#define MW_FONT_12_INCLUDED
#define MW_FONT_16_INCLUDED
#define MW_FONT_20_INCLUDED
#define MW_FONT_24_INCLUDED

#define MW_DRAG_THRESHOLD_PIXELS        2
#define MW_BUSY_TEXT                    "BUSY..."
#define MW_CALIBRATE_TEXT               "Touch centre of cross"

#ifdef __cplusplus
}
#endif
