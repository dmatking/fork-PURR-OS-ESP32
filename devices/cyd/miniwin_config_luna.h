#pragma once

// PURR OS — MiniWin configuration for CYD, Luna (Windows XP) theme
// Title bars: Luna blue (#0049CD focused, #7B96C2 unfocused)
// Dialogs/controls: silver (#D4D0C8 background, standard Windows XP grays)

#ifdef __cplusplus
extern "C" {
#endif

// ── Memory limits ─────────────────────────────────────────────────────────────
#define MW_MAX_WINDOW_COUNT         14U
#define MW_MAX_CONTROL_COUNT        24U
#define MW_MAX_TIMER_COUNT          8U
#define MW_MESSAGE_QUEUE_SIZE       100U

// ── Display rotation ──────────────────────────────────────────────────────────
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

// ── UI colours — Windows XP Luna blue ────────────────────────────────────────
// Closest flat approximations (MiniWin has no gradient support yet)
#define MW_TITLE_BAR_COLOUR_FOCUS       0x0049CDu   // Luna blue — active title bar
#define MW_TITLE_BAR_COLOUR_NO_FOCUS    0x7B96C2u   // muted blue-gray — inactive
#define MW_TITLE_BAR_COLOUR_MODAL       0xCC0000u   // red for error/modal dialogs
#define MW_TITLE_TEXT_COLOUR_FOCUS      0xFFFFFFu   // white text on blue
#define MW_TITLE_TEXT_COLOUR_NO_FOCUS   0xE8E8E8u   // near-white on inactive
#define MW_TITLE_TEXT_COLOUR_MODAL      0xFFFFFFu
#define MW_CONTROL_UP_COLOUR            0xECE9D8u   // Luna light — button face
#define MW_CONTROL_SEPARATOR_COLOUR     0xACA899u   // XP separator gray
#define MW_CONTROL_DOWN_COLOUR          0xD4D0C8u   // XP control background
#define MW_CONTROL_DISABLED_COLOUR      0xBDB9ABu   // XP disabled

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
