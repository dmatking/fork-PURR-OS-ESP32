#pragma once

// PURR OS — MiniWin configuration for LilyGO T-Deck Plus
// Display: ST7789 SPI 320x240 landscape
//   CS=12  DC=11  BL=42  MOSI=41  MISO=38  SCK=40  PWR_EN=10
// Touch: GT911 I2C
//   SDA=18  SCL=8  INT=16  RST=17  addr=0x5D

#ifdef __cplusplus
extern "C" {
#endif

#define MW_MAX_WINDOW_COUNT         14U
#define MW_MAX_CONTROL_COUNT        24U
#define MW_MAX_TIMER_COUNT          8U
#define MW_MESSAGE_QUEUE_SIZE       100U

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

#define MW_TICK_PERIOD_MS               67
#define MW_TICKS_PER_SECOND             15U
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
