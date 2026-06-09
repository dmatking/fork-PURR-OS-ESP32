// display_ili9341.cpp — ILI9341 via TFT_eSPI

#include "display_ili9341.h"
#include <TFT_eSPI.h>
#include "driver/ledc.h"
#include "esp_log.h"

static const char* TAG = "display";
static TFT_eSPI tft = TFT_eSPI();

void display_ili9341_init() {
    ESP_LOGI(TAG, "Initializing display...");
    tft.begin();
    tft.setRotation(1);

    ledc_timer_config_t tmr = {};
    tmr.speed_mode      = LEDC_LOW_SPEED_MODE;
    tmr.duty_resolution = LEDC_TIMER_8_BIT;
    tmr.timer_num       = LEDC_TIMER_1;
    tmr.freq_hz         = 5000;
    tmr.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&tmr);

    ledc_channel_config_t ch = {};
    ch.gpio_num   = CYD_TFT_BL;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = LEDC_CHANNEL_1;
    ch.timer_sel  = LEDC_TIMER_1;
    ch.duty       = 255;
    ledc_channel_config(&ch);

#ifdef PURR_HAS_LVGL
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[CYD_TFT_WIDTH * 10];
    static lv_color_t buf2[CYD_TFT_WIDTH * 10];
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, CYD_TFT_WIDTH * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = CYD_TFT_WIDTH;
    disp_drv.ver_res = CYD_TFT_HEIGHT;
    disp_drv.flush_cb = display_ili9341_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
#endif

    ESP_LOGI(TAG, "Display ready");
}

void display_ili9341_update() {}
void display_ili9341_deinit() {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    tft.writecommand(0x28);
}
void display_ili9341_set_brightness(uint8_t level) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

void display_ili9341_clear() { tft.fillScreen(TFT_BLACK); }
void display_ili9341_text(uint8_t row, const char* text) { (void)row; (void)text; }
void display_ili9341_set_text_colors(uint16_t fg, uint16_t bg) { (void)fg; (void)bg; }
void display_ili9341_push_block(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    tft.fillRect(x, y, w, h, color);
}
void display_ili9341_push_colors(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* colors) {
    if (!colors) return;
    tft.setAddrWindow(x, y, w, h);
    tft.pushColors((uint16_t*)colors, w * h, true);
}
void display_ili9341_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    tft.fillRect(x, y, w, h, color);
}
void display_ili9341_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    tft.drawFastHLine(x, y, w, color);
}
void display_ili9341_draw_string(int16_t x, int16_t y, const char* s, uint16_t fg, uint16_t bg, uint8_t size) {
    if (!s) return;
    tft.setTextColor(fg, bg);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print(s);
}

#ifdef PURR_HAS_LVGL
void display_ili9341_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}
#endif
