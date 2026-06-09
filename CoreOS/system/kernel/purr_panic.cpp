#include "purr_panic.h"
#include "esp_system.h"
#include "esp_log.h"
#include <stdio.h>

#ifdef PURR_DISPLAY_ILI9341
#  include "display_ili9341.h"
// RGB565 constants
#  define PX_RED    0xF800u
#  define PX_BLUE   0x001Fu
#  define PX_WHITE  0xFFFFu
#  define PX_YELLOW 0xFFE0u
#  define PX_BLACK  0x0000u
#endif

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
    printf("\n!!! PURR KERNEL PANIC [%s] !!!\n", stop_code);
    printf("Reason: %s\n", msg ? msg : "(none)");
#ifdef PURR_HAS_MINIWIN
    printf("--- RINGBUFFER DUMP ---\n");
    int start = (purr_log_head - purr_log_count + PURR_LOG_LINES) % PURR_LOG_LINES;
    for (int i = 0; i < purr_log_count; i++)
        printf("%s\n", purr_log_ring[(start + i) % PURR_LOG_LINES]);
    printf("--- END DUMP ---\n\n");
#endif

#ifdef PURR_DISPLAY_ILI9341
    uint16_t bg = (level == PURR_PANIC_RED) ? PX_RED : PX_BLUE;
    display_ili9341_fill_rect(0, 0, 320, 240, bg);

    char line[64];
    const char *face = (level == PURR_PANIC_RED) ? ":-(" : ":-/";
    display_ili9341_draw_string(20, 20, face,   PX_WHITE, bg, 4);
    display_ili9341_draw_string(20, 80, "PURR OS Error", PX_WHITE, bg, 2);

    snprintf(line, sizeof(line), "Stop: %s", stop_code ? stop_code : "");
    display_ili9341_draw_string(20, 120, line,  PX_WHITE, bg, 1);
    if (msg) display_ili9341_draw_string(20, 136, msg, PX_WHITE, bg, 1);

    if (level == PURR_PANIC_RED) {
        display_ili9341_draw_string(20, 170, "CRITICAL: SYSTEM HALTED", PX_YELLOW, bg, 1);
        while (1) esp_rom_delay_us(1000000);
    } else {
        display_ili9341_draw_string(20, 170, "Reset to recover", PX_WHITE, bg, 1);
    }
#else
    if (level == PURR_PANIC_RED)
        while (1) esp_rom_delay_us(1000000);
#endif
}
