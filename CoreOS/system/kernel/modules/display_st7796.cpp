#ifdef PURR_DISPLAY_ST7796

#include "display_st7796.h"
#include <TFT_eSPI.h>
#include <Arduino.h>

static TFT_eSPI tft = TFT_eSPI();
static uint16_t _text_fg = 0xFFFF;
static uint16_t _text_bg = 0x0000;

void display_st7796_init() {
    tft.begin();
    tft.setRotation(1);  // landscape

    ledcAttach(ST7796_TFT_BL, 5000, 8);
    ledcWrite(ST7796_TFT_BL, 255);

    Serial.printf("[disp] ST7796 OK %dx%d\n", ST7796_TFT_WIDTH, ST7796_TFT_HEIGHT);
}

void display_st7796_update() {}

void display_st7796_deinit() {
    ledcWrite(ST7796_TFT_BL, 0);
    tft.writecommand(0x28);
}

void display_st7796_set_brightness(uint8_t level) {
    ledcWrite(ST7796_TFT_BL, level);
}

void display_st7796_clear() {
    tft.fillScreen(_text_bg);
}

void display_st7796_text(uint8_t row, const char* text) {
    tft.setTextSize(2);
    tft.setTextColor(_text_fg, _text_bg);
    tft.setCursor(4, 4 + row * 18);
    tft.print(text);
}

void display_st7796_set_text_colors(uint16_t fg_rgb565, uint16_t bg_rgb565) {
    _text_fg = fg_rgb565;
    _text_bg = bg_rgb565;
}

void display_st7796_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    tft.fillRect(x, y, w, h, color);
}

void display_st7796_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    tft.drawFastHLine(x, y, w, color);
}

void display_st7796_draw_string(int16_t x, int16_t y, const char* s,
                                 uint16_t fg, uint16_t bg, uint8_t size) {
    tft.setTextSize(size);
    tft.setTextColor(fg, bg);
    tft.setCursor(x, y);
    tft.print(s);
}

#endif  // PURR_DISPLAY_ST7796
