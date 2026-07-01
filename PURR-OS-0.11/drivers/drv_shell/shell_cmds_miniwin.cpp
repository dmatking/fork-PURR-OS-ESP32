#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PURR_HAS_MINIWIN
#include "miniwin.h"
#include "miniwin_touch.h"
#include "miniwin_settings.h"
#include "hal/hal_lcd.h"
#include "hal/hal_touch.h"
#ifdef PURR_DISPLAY_ILI9341
#  include "display_ili9341.h"
#endif
#ifdef PURR_HAS_TOUCH_XPT2046
#  include "touch_xpt2046.h"
#elif defined(PURR_HAS_TOUCH_GT911)
#  include "touch_gt911.h"
#elif defined(PURR_HAS_TOUCH_CST816S)
#  include "touch_cst816s.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {

// mw-paint — queue a full repaint via mw_paint_all()
void cmd_mw_paint(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("mw_paint_all()...\n");
    mw_paint_all();
    printf("Done (messages queued).\n");
}

// mw-tick [N] — drain up to N messages from the queue (default 200)
void cmd_mw_tick(int argc, char **argv)
{
    int n = (argc >= 2) ? atoi(argv[1]) : 200;
    int processed = 0;
    for (int i = 0; i < n; i++) {
        if (mw_process_message()) processed++;
    }
    printf("Processed %d/%d messages.\n", processed, n);
}

// mw-rect x y w h color — draw a rectangle directly via HAL (bypasses message queue)
void cmd_mw_rect(int argc, char **argv)
{
    if (argc < 6) {
        printf("usage: mw-rect <x> <y> <w> <h> <0xRRGGBB>\n");
        printf("  example: mw-rect 10 10 100 80 0xFF0000  (red)\n");
        return;
    }
    int16_t x = (int16_t)atoi(argv[1]);
    int16_t y = (int16_t)atoi(argv[2]);
    int16_t w = (int16_t)atoi(argv[3]);
    int16_t h = (int16_t)atoi(argv[4]);
    mw_hal_lcd_colour_t rgb = (mw_hal_lcd_colour_t)strtoul(argv[5], NULL, 0);

    // Convert 0xRRGGBB to RGB565 and push directly to display
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >>  8) & 0xFF;
    uint8_t b =  rgb        & 0xFF;
    uint16_t rgb565 = ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);

    printf("HAL rect %d,%d %dx%d rgb565=0x%04X\n", x, y, w, h, rgb565);
#ifdef PURR_DISPLAY_ILI9341
    display_ili9341_push_block(x, y, w, h, rgb565);
#else
    (void)rgb565;
    printf("mw-rect: direct push not supported on this display\n");
#endif
    printf("Done.\n");
}

// mw-init — re-run mw_init() to reset MiniWin state
void cmd_mw_init(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("mw_init()...\n");
    mw_init();
    printf("Done.\n");
}

// mw-touch [N] — poll touch N times (20 ms each, default 150 = ~3s), print each event
// Output columns: raw_cst=x,y  scaled=x,y  display=x,y
void cmd_mw_touch(int argc, char **argv)
{
    int n = (argc >= 2) ? atoi(argv[1]) : 150;

    printf("cal=%d init=%d  polling %d ticks (20ms each)...\n",
           mw_settings_is_calibrated() ? 1 : 0,
           mw_settings_is_initialised() ? 1 : 0,
           n);

#if !defined(PURR_HAS_TOUCH_XPT2046) && !defined(PURR_HAS_TOUCH_GT911) && !defined(PURR_HAS_TOUCH_CST816S)
    printf("No touch controller on this target.\n");
    return;
#endif
    for (int i = 0; i < n; i++) {
#ifdef PURR_HAS_TOUCH_XPT2046
        xpt_touch_event_t ev = {};
        touch_xpt2046_get_event(&ev);
        int raw_x = ev.x, raw_y = ev.y;
#elif defined(PURR_HAS_TOUCH_GT911)
        gt911_touch_event_t ev = {};
        touch_gt911_get_event(&ev);
        int raw_x = ev.x, raw_y = ev.y;
#elif defined(PURR_HAS_TOUCH_CST816S)
        cst_touch_event_t ev = {};
        touch_cst816s_get_event(&ev);
        int raw_x = ev.x, raw_y = ev.y;
#else
        struct { bool pressed; int x, y; } ev = {};
        int raw_x = 0, raw_y = 0;
#endif

        if (ev.pressed) {
            mw_hal_touch_get_state();

            int16_t dx = 0, dy = 0;
            mw_touch_get_display_touch(&dx, &dy);

            printf("TOUCH raw=%d,%d  display=%d,%d\n", raw_x, raw_y, dx, dy);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("Done.\n");
}

} // extern "C"

#else

extern "C" {
void cmd_mw_paint (int argc, char **argv) { (void)argc; (void)argv; printf("MiniWin not active on this target.\n"); }
void cmd_mw_tick  (int argc, char **argv) { (void)argc; (void)argv; printf("MiniWin not active on this target.\n"); }
void cmd_mw_rect  (int argc, char **argv) { (void)argc; (void)argv; printf("MiniWin not active on this target.\n"); }
void cmd_mw_init  (int argc, char **argv) { (void)argc; (void)argv; printf("MiniWin not active on this target.\n"); }
void cmd_mw_touch (int argc, char **argv) { (void)argc; (void)argv; printf("MiniWin not active on this target.\n"); }
}

#endif // PURR_HAS_MINIWIN
