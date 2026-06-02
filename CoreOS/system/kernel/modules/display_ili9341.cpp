#include "display_ili9341.h"
#include <TFT_eSPI.h>
#include <Arduino.h>

static TFT_eSPI tft = TFT_eSPI();

void display_ili9341_init() {
    tft.begin();
    tft.setRotation(1);  // landscape, USB on right

    ledcAttach(CYD_TFT_BL, 5000, 8);
    ledcWrite(CYD_TFT_BL, 255);

#ifdef PURR_HAS_LVGL
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[CYD_TFT_WIDTH * 10];
    static lv_color_t buf2[CYD_TFT_WIDTH * 10];

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, CYD_TFT_WIDTH * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = CYD_TFT_WIDTH;
    disp_drv.ver_res  = CYD_TFT_HEIGHT;
    disp_drv.flush_cb = display_ili9341_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
#endif

    Serial.printf("[disp] ILI9341 OK %dx%d\n", CYD_TFT_WIDTH, CYD_TFT_HEIGHT);
}

void display_ili9341_update() {}

void display_ili9341_deinit() {
    ledcWrite(CYD_TFT_BL, 0);
    tft.writecommand(0x28);
}

void display_ili9341_set_brightness(uint8_t level) {
    ledcWrite(CYD_TFT_BL, level);
}

// ── Row-based text layer ───────────────────────────────────────────────────────

static uint16_t _text_fg = 0xFFFF;
static uint16_t _text_bg = 0x0000;

void display_ili9341_clear() {
    tft.fillScreen(_text_bg);
}

void display_ili9341_text(uint8_t row, const char* text) {
    tft.setTextSize(2);
    tft.setTextColor(_text_fg, _text_bg);
    tft.setCursor(4, 4 + row * 18);
    tft.print(text);
}

void display_ili9341_set_text_colors(uint16_t fg_rgb565, uint16_t bg_rgb565) {
    _text_fg = fg_rgb565;
    _text_bg = bg_rgb565;
}

// ── Drawing primitives ────────────────────────────────────────────────────────

void display_ili9341_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    tft.fillRect(x, y, w, h, color);
}

void display_ili9341_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    tft.drawFastHLine(x, y, w, color);
}

void display_ili9341_draw_string(int16_t x, int16_t y, const char* s,
                                  uint16_t fg, uint16_t bg, uint8_t size) {
    tft.setTextSize(size);
    tft.setTextColor(fg, bg);
    tft.setCursor(x, y);
    tft.print(s);
}

// ── LVGL flush (only compiled when LVGL is in the build) ─────────────────────

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
