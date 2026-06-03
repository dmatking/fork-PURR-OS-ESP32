#include "display_ili9488.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

// Pin config lives in TFT_eSPI User_Setup.h for CattoPad:
// TFT_MOSI=11, TFT_SCLK=12, TFT_CS=10, TFT_DC=9, TFT_RST=8, TFT_BL=46

static TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[TFT_WIDTH * 10];
static lv_color_t buf2[TFT_WIDTH * 10];

void display_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

void display_ili9488_init() {
    tft.begin();
    tft.setRotation(0);  // portrait, USB at bottom

    // Backlight via LEDC PWM (arduino-esp32 3.x pin-based API)
    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, 255);

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, TFT_WIDTH * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = TFT_WIDTH;
    disp_drv.ver_res  = TFT_HEIGHT;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    Serial.printf("[disp] ILI9488 OK %dx%d\n", TFT_WIDTH, TFT_HEIGHT);
}

void display_ili9488_update() {
    // no-op — LVGL drives rendering via lv_timer_handler()
}

void display_ili9488_deinit() {
    ledcWrite(TFT_BL, 0);
    tft.writecommand(0x28);  // display off
}

void display_ili9488_set_brightness(uint8_t level) {
    ledcWrite(TFT_BL, level);
}
