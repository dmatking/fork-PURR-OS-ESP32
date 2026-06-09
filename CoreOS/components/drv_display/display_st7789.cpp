// display_st7789.cpp — ST7789 display driver via esp_lcd (pure ESP-IDF)
// Default: Waveshare 1.69" 240x280. Pin/res overrides via PURR_ST7789_* defines.

#include "display_st7789.h"
#include "display_font5x7.h"

#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "st7789";

// SPI host: use SPI2 if the display shares SPI2 pins, else SPI3 (default)
#ifndef PURR_ST7789_SPI_HOST
#  define PURR_ST7789_SPI_HOST SPI3_HOST
#endif
#ifndef PURR_ST7789_MOSI
#  define PURR_ST7789_MOSI 13
#endif
#ifndef PURR_ST7789_SCLK
#  define PURR_ST7789_SCLK 11
#endif
#ifndef PURR_ST7789_CS
#  define PURR_ST7789_CS   12
#endif
#ifndef PURR_ST7789_DC
#  define PURR_ST7789_DC   10
#endif
#ifndef PURR_ST7789_RST
#  define PURR_ST7789_RST  14
#endif

#define LCD_HOST    PURR_ST7789_SPI_HOST
#define LCD_MOSI    PURR_ST7789_MOSI
#define LCD_SCLK    PURR_ST7789_SCLK
#define LCD_MISO    -1
#define LCD_CS      PURR_ST7789_CS
#define LCD_DC      PURR_ST7789_DC
#define LCD_RST     PURR_ST7789_RST
#define LCD_CLK_HZ  (40 * 1000 * 1000)

#define BL_MODE     LEDC_LOW_SPEED_MODE
#define BL_TIMER    LEDC_TIMER_1
#define BL_CHANNEL  LEDC_CHANNEL_1

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io    = NULL;

static uint16_t _text_bg = 0x0000;
static bool _ready = false;

static void bl_init(void) {
    ledc_timer_config_t t = { .speed_mode = BL_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                               .timer_num = BL_TIMER, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&t);
    ledc_channel_config_t ch = { .gpio_num = ST7789_TFT_BL, .speed_mode = BL_MODE,
                                  .channel = BL_CHANNEL, .timer_sel = BL_TIMER, .duty = 0 };
    ledc_channel_config(&ch);
}
static void bl_set(uint8_t v) {
    ledc_set_duty(BL_MODE, BL_CHANNEL, v);
    ledc_update_duty(BL_MODE, BL_CHANNEL);
}

void display_st7789_init() {
    if (_ready) return; _ready = true;

    spi_bus_config_t bus = { .mosi_io_num = LCD_MOSI, .miso_io_num = LCD_MISO,
                              .sclk_io_num = LCD_SCLK, .quadwp_io_num = -1,
                              .quadhd_io_num = -1,
                              .max_transfer_sz = ST7789_TFT_WIDTH * ST7789_TFT_HEIGHT * 2 };
    spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_spi_config_t io_cfg = { .cs_gpio_num = LCD_CS, .dc_gpio_num = LCD_DC,
                                              .spi_clock_hz = LCD_CLK_HZ,
                                              .lcd_cmd_bits = 8, .lcd_param_bits = 8,
                                              .trans_queue_depth = 10 };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io);

    esp_lcd_panel_dev_config_t panel_cfg = { .reset_gpio_num = LCD_RST,
                                              .rgb_endian = LCD_RGB_ENDIAN_RGB,
                                              .bits_per_pixel = 16 };
    esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel);
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_disp_on_off(s_panel, true);

    bl_init(); bl_set(255);
    ESP_LOGI(TAG, "OK %dx%d", ST7789_TFT_WIDTH, ST7789_TFT_HEIGHT);
}

void display_st7789_update()  {}
void display_st7789_deinit()  { bl_set(0); esp_lcd_panel_disp_on_off(s_panel, false); }
void display_st7789_set_brightness(uint8_t v) { bl_set(v); }
void display_st7789_set_text_colors(uint16_t, uint16_t bg) { _text_bg = bg; }

void display_st7789_clear() {
    display_st7789_fill_rect(0, 0, ST7789_TFT_WIDTH, ST7789_TFT_HEIGHT, _text_bg);
}
void display_st7789_text(uint8_t, const char*) {}   // LVGL handles text
void display_st7789_draw_string(int16_t x, int16_t y, const char* s, uint16_t fg, uint16_t bg, uint8_t size) {
    display_font5x7_draw_string(x, y, s, fg, bg, size, display_st7789_fill_rect);
}

void display_st7789_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!s_panel || w <= 0 || h <= 0) return;
    static uint16_t row[480];
    int fill = (w < 480) ? w : 480;
    uint16_t swapped = (color >> 8) | (color << 8);
    for (int i = 0; i < fill; i++) row[i] = swapped;
    for (int r = 0; r < h; r++)
        esp_lcd_panel_draw_bitmap(s_panel, x, y + r, x + w, y + r + 1, row);
}

void display_st7789_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    display_st7789_fill_rect(x, y, w, 1, color);
}

void display_st7789_push_block(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    display_st7789_fill_rect(x, y, w, h, color);
}

void display_st7789_push_colors(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* colors) {
    if (!s_panel || !colors || w <= 0 || h <= 0) return;
    // esp_lcd_panel_draw_bitmap expects big-endian RGB565 — byte-swap each pixel
    static uint16_t row[480];
    for (int r = 0; r < h; r++) {
        const uint16_t* src = colors + r * w;
        int cnt = (w <= 480) ? w : 480;
        for (int i = 0; i < cnt; i++) row[i] = (uint16_t)((src[i] >> 8) | (src[i] << 8));
        esp_lcd_panel_draw_bitmap(s_panel, x, y + r, x + w, y + r + 1, row);
    }
}

