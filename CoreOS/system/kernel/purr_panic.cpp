#include "purr_panic.h"
#include "esp_system.h"
#include "esp_log.h"
#include <stdio.h>

#ifdef PURR_DISPLAY_ILI9341
#  include "display_ili9341.h"
#  define PANIC_SCR_W  320
#  define PANIC_SCR_H  240
#  define _fill_rect   display_ili9341_fill_rect
#  define _draw_str    display_ili9341_draw_string
#elif defined(PURR_DISPLAY_ST7796)
#  include "display_st7796.h"
#  define PANIC_SCR_W  480
#  define PANIC_SCR_H  320
#  define _fill_rect   display_st7796_fill_rect
#  define _draw_str    display_st7796_draw_string
#elif defined(PURR_DISPLAY_ST7789)
#  include "display_st7789.h"
#  define PANIC_SCR_W  320
#  define PANIC_SCR_H  240
#  define _fill_rect   display_st7789_fill_rect
#  define _draw_str    display_st7789_draw_string
#endif

// RGB565
#define PX_RED    0xF800u
#define PX_BLUE   0x001Fu
#define PX_WHITE  0xFFFFu
#define PX_YELLOW 0xFFE0u
#define PX_BLACK  0x0000u

// Ring buffer defined in devices/apps/purr_log.cpp — only compiled with MiniWin
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
    const bool is_red = (level == PURR_PANIC_RED);
    const uint16_t bg     = is_red ? PX_RED  : PX_BLUE;
    const uint16_t accent = is_red ? PX_YELLOW : PX_WHITE;

    _fill_rect(0, 0, PANIC_SCR_W, PANIC_SCR_H, bg);

    // Face — large, top-left
    const char *face      = is_red ? ":-("           : ":-/";
    const char *title     = is_red ? "SYSTEM CRASHED" : "SYSTEM UNSTABLE";
    const char *footer    = is_red ? "Rebooting..." : "Tap to continue with instability";

    _draw_str(16, 16,  face,   PX_WHITE, bg, 4);   // ~48px tall
    _draw_str(16, 80,  title,  accent,   bg, 2);   // ~16px tall
    _draw_str(16, 108, "PURR OS  |  Kernel Panic", PX_WHITE, bg, 1);

    char code_line[64];
    __builtin_snprintf(code_line, sizeof(code_line), "Stop: %s", stop_code ? stop_code : "");
    _draw_str(16, 132, code_line, PX_WHITE, bg, 1);
    if (msg)
        _draw_str(16, 148, msg, PX_WHITE, bg, 1);

    _draw_str(16, PANIC_SCR_H - 24, footer, accent, bg, 1);
#endif

    if (level == PURR_PANIC_RED) {
        esp_rom_delay_us(10000000); // show screen for 10s
        esp_restart();
    }
}
