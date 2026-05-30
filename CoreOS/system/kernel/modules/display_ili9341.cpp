#include "display_ili9341.h"
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <Arduino.h>

static TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[CYD_TFT_WIDTH * 10];
static lv_color_t buf2[CYD_TFT_WIDTH * 10];

void display_ili9341_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

void display_ili9341_init() {
    tft.begin();
    tft.setRotation(1);  // landscape, USB on right

    // Backlight via LEDC PWM
    ledcSetup(1, 5000, 8);
    ledcAttachPin(CYD_TFT_BL, 1);
    ledcWrite(1, 255);

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, CYD_TFT_WIDTH * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = CYD_TFT_WIDTH;
    disp_drv.ver_res  = CYD_TFT_HEIGHT;
    disp_drv.flush_cb = display_ili9341_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    Serial.printf("[disp] ILI9341 OK %dx%d\n", CYD_TFT_WIDTH, CYD_TFT_HEIGHT);
}

void display_ili9341_update() {}

void display_ili9341_deinit() {
    ledcWrite(1, 0);
    tft.writecommand(0x28);  // display off
}

void display_ili9341_set_brightness(uint8_t level) {
    ledcWrite(1, level);
}
