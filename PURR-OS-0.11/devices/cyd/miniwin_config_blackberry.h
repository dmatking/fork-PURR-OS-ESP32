#pragma once

// PURR OS — MiniWin configuration for CYD, Blackberry theme
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

// ── UI colours — phosphor green-on-black ─────────────────────────────────────
#define MW_TITLE_BAR_COLOUR_FOCUS       0x008818u   // BB_GREEN_MID  — focused title bar
#define MW_TITLE_BAR_COLOUR_NO_FOCUS    0x002200u   // BB_ICON_BG    — unfocused (dim)
#define MW_TITLE_BAR_COLOUR_MODAL       0x220000u   // BB_ADMIN_BG   — modal/alert (red tint)
#define MW_TITLE_TEXT_COLOUR_FOCUS      0x00FF44u   // BB_GREEN_HI   — bright green text
#define MW_TITLE_TEXT_COLOUR_NO_FOCUS   0x004410u   // BB_GREEN_DIM  — dim green text
#define MW_TITLE_TEXT_COLOUR_MODAL      0xFF4444u   // red text for modal
#define MW_CONTROL_UP_COLOUR            0x002200u   // BB_ICON_BG
#define MW_CONTROL_SEPARATOR_COLOUR     0x003000u   // BB_SEP
#define MW_CONTROL_DOWN_COLOUR          0x001800u   // BB_PANEL
#define MW_CONTROL_DISABLED_COLOUR      0x000800u   // BB_BG_TINT

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
