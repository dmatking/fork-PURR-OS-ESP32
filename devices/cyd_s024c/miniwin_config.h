#pragma once

// PURR OS — MiniWin configuration for CYD (ESP32-2432S024C)
// Display: ILI9341, 320x240, landscape
// Touch:   CST816S capacitive (pre-calibrated, no resistive cal needed)

#ifdef __cplusplus
extern "C" {
#endif

// ── Memory limits ─────────────────────────────────────────────────────────────
#define MW_MAX_WINDOW_COUNT         14U
#define MW_MAX_CONTROL_COUNT        24U
#define MW_MAX_TIMER_COUNT          8U
#define MW_MESSAGE_QUEUE_SIZE       100U

// ── Display rotation ──────────────────────────────────────────────────────────
// Landscape 320x240 — rotation 0 (width > height, no swap needed)
#define MW_DISPLAY_ROTATION_0

#if !defined(MW_DISPLAY_ROTATION_0) && !defined(MW_DISPLAY_ROTATION_90) \
 && !defined(MW_DISPLAY_ROTATION_180) && !defined(MW_DISPLAY_ROTATION_270)
#error Display rotation not defined
#endif

// ── Root window dimensions ────────────────────────────────────────────────────
#if defined(MW_DISPLAY_ROTATION_0) || defined(MW_DISPLAY_ROTATION_180)
#define MW_ROOT_WIDTH               mw_hal_lcd_get_display_width()
#define MW_ROOT_HEIGHT              mw_hal_lcd_get_display_height()
#elif defined(MW_DISPLAY_ROTATION_90) || defined(MW_DISPLAY_ROTATION_270)
#define MW_ROOT_WIDTH               mw_hal_lcd_get_display_height()
#define MW_ROOT_HEIGHT              mw_hal_lcd_get_display_width()
#endif

#define MW_MAX_TITLE_SIZE           14U

// ── UI colours ────────────────────────────────────────────────────────────────
#define MW_TITLE_BAR_COLOUR_FOCUS       MW_HAL_LCD_BLUE
#define MW_TITLE_BAR_COLOUR_NO_FOCUS    MW_HAL_LCD_GREY5
#define MW_TITLE_BAR_COLOUR_MODAL       MW_HAL_LCD_RED
#define MW_TITLE_TEXT_COLOUR_FOCUS      MW_HAL_LCD_WHITE
#define MW_TITLE_TEXT_COLOUR_NO_FOCUS   MW_HAL_LCD_WHITE
#define MW_TITLE_TEXT_COLOUR_MODAL      MW_HAL_LCD_WHITE
#define MW_CONTROL_UP_COLOUR            MW_HAL_LCD_GREY2
#define MW_CONTROL_SEPARATOR_COLOUR     MW_HAL_LCD_GREY3
#define MW_CONTROL_DOWN_COLOUR          MW_HAL_LCD_GREY4
#define MW_CONTROL_DISABLED_COLOUR      MW_HAL_LCD_GREY5

// ── Timings (tick = 20 ms, so MW_TICKS_PER_SECOND = 50) ──────────────────────
#define MW_TICK_PERIOD_MS               20
#define MW_TICKS_PER_SECOND             50U
#define MW_CONTROL_DOWN_TIME            4U
#define MW_KEY_DOWN_TIME                3U
#define MW_WINDOW_MIN_MAX_EFFECT_TIME   5U
#define MW_CURSOR_PERIOD_TICKS          10U
#define MW_TOUCH_INTERVAL_TICKS         2U
#define MW_HOLD_DOWN_DELAY_TICKS        10U

// ── Fonts ─────────────────────────────────────────────────────────────────────
#define MW_FONT_12_INCLUDED
#define MW_FONT_16_INCLUDED
#define MW_FONT_20_INCLUDED
#define MW_FONT_24_INCLUDED

// ── Other ─────────────────────────────────────────────────────────────────────
#define MW_DRAG_THRESHOLD_PIXELS        2
#define MW_BUSY_TEXT                    "BUSY..."
#define MW_CALIBRATE_TEXT               "Touch centre of cross"

#ifdef __cplusplus
}
#endif
