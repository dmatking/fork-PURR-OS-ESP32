#include "purr_panic.h"
#include "esp_system.h"
#include "esp_log.h"
#include <stdio.h>

// When MiniWin is compiled in, the display is driven through mw_hal_lcd_*
// (e.g. hal_lcd.cpp on T-Deck Plus) and the raw display_*_fill_rect functions
// have a separate, never-initialized panel handle.  Route panic drawing through
// the HAL so it works regardless of which driver owns the panel.
#ifdef PURR_HAS_MINIWIN
#  include "hal/hal_lcd.h"
#  include "display_font5x7.h"

static void _panic_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t rgb565) {
    uint8_t r = (uint8_t)(((rgb565 >> 11) & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((rgb565 >>  5) & 0x3Fu) << 2);
    uint8_t b = (uint8_t)(( rgb565        & 0x1Fu) << 3);
    mw_hal_lcd_colour_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    mw_hal_lcd_filled_rectangle(x, y, w, h, c);
}
static void _panic_str(int16_t x, int16_t y, const char *s,
                        uint16_t fg, uint16_t bg, uint8_t sz) {
    display_font5x7_draw_string(x, y, s, fg, bg, sz, _panic_fill);
}
#  define _fill_rect  _panic_fill
#  define _draw_str   _panic_str
#  define PANIC_SCR_W ((int16_t)mw_hal_lcd_get_display_width())
#  define PANIC_SCR_H ((int16_t)mw_hal_lcd_get_display_height())

#else
// No MiniWin — use raw driver functions directly (they own the panel)
#  ifdef PURR_DISPLAY_ILI9341
#    include "display_ili9341.h"
#    define PANIC_SCR_W  320
#    define PANIC_SCR_H  240
#    define _fill_rect   display_ili9341_fill_rect
#    define _draw_str    display_ili9341_draw_string
#  elif defined(PURR_DISPLAY_ST7796)
#    include "display_st7796.h"
#    define PANIC_SCR_W  480
#    define PANIC_SCR_H  320
#    define _fill_rect   display_st7796_fill_rect
#    define _draw_str    display_st7796_draw_string
#  elif defined(PURR_DISPLAY_ST7789)
#    include "display_st7789.h"
#    define PANIC_SCR_W  320
#    define PANIC_SCR_H  240
#    define _fill_rect   display_st7789_fill_rect
#    define _draw_str    display_st7789_draw_string
#  endif
#endif

// RGB565 colour constants
#define PX_RED    0xF800u
#define PX_BLUE   0x001Fu
#define PX_WHITE  0xFFFFu
#define PX_YELLOW 0xFFE0u

// Ring buffer from purr_log.cpp (MiniWin builds only)
#ifdef PURR_HAS_MINIWIN
#  define PURR_LOG_LINES    12
#  define PURR_LOG_LINE_LEN 40
extern char purr_log_ring[PURR_LOG_LINES][PURR_LOG_LINE_LEN];
extern int  purr_log_head;
extern int  purr_log_count;
#endif

void purr_panic(const char* stop_code, purr_panic_level_t level, const char* msg)
{
    printf("\n!!! PURR KERNEL PANIC [%s] !!!\n", stop_code ? stop_code : "?");
    printf("Level : %s\n", level == PURR_PANIC_RED ? "RED  :-(" : "BLUE :-/");
    printf("Reason: %s\n", msg ? msg : "(none)");
#ifdef PURR_HAS_MINIWIN
    printf("--- RINGBUFFER DUMP ---\n");
    int start = (purr_log_head - purr_log_count + PURR_LOG_LINES) % PURR_LOG_LINES;
    for (int i = 0; i < purr_log_count; i++)
        printf("%s\n", purr_log_ring[(start + i) % PURR_LOG_LINES]);
    printf("--- END DUMP ---\n\n");
#endif

#if defined(_fill_rect) && defined(_draw_str)
    const bool is_red  = (level == PURR_PANIC_RED);
    const uint16_t bg     = is_red ? PX_RED  : PX_BLUE;
    const uint16_t accent = is_red ? PX_YELLOW : PX_WHITE;

    _fill_rect(0, 0, PANIC_SCR_W, PANIC_SCR_H, bg);

    const char *face   = is_red ? ":-("            : ":-/";
    const char *title  = is_red ? "SYSTEM CRASHED" : "SYSTEM UNSTABLE";
    const char *footer = is_red ? "Rebooting..."   : "Tap to continue with instability";

    _draw_str(16, 16,  face,                      PX_WHITE, bg, 4);
    _draw_str(16, 80,  title,                     accent,   bg, 2);
    _draw_str(16, 108, "PURR OS  |  Kernel Panic", PX_WHITE, bg, 1);

    char code_line[64];
    __builtin_snprintf(code_line, sizeof(code_line), "Stop: %s", stop_code ? stop_code : "");
    _draw_str(16, 132, code_line, PX_WHITE, bg, 1);
    if (msg)
        _draw_str(16, 148, msg, PX_WHITE, bg, 1);

    _draw_str(16, (int16_t)(PANIC_SCR_H - 24), footer, accent, bg, 1);
#endif

    if (level == PURR_PANIC_RED) {
        esp_rom_delay_us(10000000);
        esp_restart();
    }
}
