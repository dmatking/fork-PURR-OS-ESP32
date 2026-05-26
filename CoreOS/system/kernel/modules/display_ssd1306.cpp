#include "display_ssd1306.h"
#include <Wire.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
static bool oled_ok = false;

void display_ssd1306_init() {
    Wire.begin(OLED_SDA, OLED_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[disp] SSD1306 init failed");
        return;
    }
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.display();
    oled_ok = true;
    Serial.println("[disp] SSD1306 OK 128x64");
}

void display_ssd1306_update() {
    // no-op — callers drive display via text/clear calls
}

void display_ssd1306_deinit() {
    if (oled_ok) {
        oled.ssd1306_command(SSD1306_DISPLAYOFF);
        oled_ok = false;
    }
}

void display_ssd1306_text(uint8_t row, const char* text) {
    if (!oled_ok) return;
    oled.fillRect(0, row * 8, SCREEN_WIDTH, 8, SSD1306_BLACK);
    oled.setCursor(0, row * 8);
    oled.print(text);
    oled.display();
}

void display_ssd1306_clear() {
    if (!oled_ok) return;
    oled.clearDisplay();
    oled.display();
}

void display_ssd1306_set_cursor(uint8_t x, uint8_t y) {
    if (!oled_ok) return;
    oled.setCursor(x, y);
}

void display_ssd1306_print(const char* text) {
    if (!oled_ok) return;
    oled.print(text);
    oled.display();
}
