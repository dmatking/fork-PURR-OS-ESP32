// display_st7796.cpp — ST7796 display driver via esp_lcd (pure ESP-IDF)
// Target: JC3248W535 3.5" 480x320

#ifdef PURR_DISPLAY_ST7796

#include "display_st7796.h"
#include "../purr_idf_compat.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "st7796";

#define LCD_HOST    SPI2_HOST
#define LCD_MOSI    13
#define LCD_SCLK    12
#define LCD_MISO    -1
#define LCD_CS      10
#define LCD_DC      11
#define LCD_RST     -1
#define LCD_CLK_HZ  (40 * 1000 * 1000)

#define BL_MODE     LEDC_LOW_SPEED_MODE
#define BL_TIMER    LEDC_TIMER_2
#define BL_CHANNEL  LEDC_CHANNEL_2

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io    = NULL;
static uint16_t _text_bg = 0x0000;
static bool _ready = false;

static void bl_init(void) {
    ledc_timer_config_t t = { .speed_mode = BL_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                               .timer_num = BL_TIMER, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&t);
    ledc_channel_config_t ch = { .gpio_num = ST7796_TFT_BL, .speed_mode = BL_MODE,
                                  .channel = BL_CHANNEL, .timer_sel = BL_TIMER, .duty = 0 };
    ledc_channel_config(&ch);
}
static void bl_set(uint8_t v) {
    ledc_set_duty(BL_MODE, BL_CHANNEL, v);
    ledc_update_duty(BL_MODE, BL_CHANNEL);
}

void display_st7796_init() {
    if (_ready) return; _ready = true;

    spi_bus_config_t bus = { .mosi_io_num = LCD_MOSI, .miso_io_num = LCD_MISO,
                              .sclk_io_num = LCD_SCLK, .quadwp_io_num = -1,
                              .quadhd_io_num = -1,
                              .max_transfer_sz = ST7796_TFT_WIDTH * 20 * 2 };
    spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_spi_config_t io_cfg = { .cs_gpio_num = LCD_CS, .dc_gpio_num = LCD_DC,
                                              .spi_clock_hz = LCD_CLK_HZ,
                                              .lcd_cmd_bits = 8, .lcd_param_bits = 8,
                                              .trans_queue_depth = 10 };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io);

    // ST7796 uses ST7789-compatible driver
    esp_lcd_panel_dev_config_t panel_cfg = { .reset_gpio_num = LCD_RST,
                                              .rgb_endian = LCD_RGB_ENDIAN_BGR,
                                              .bits_per_pixel = 16 };
    esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel);
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_disp_on_off(s_panel, true);

    bl_init(); bl_set(255);
    ESP_LOGI(TAG, "OK %dx%d", ST7796_TFT_WIDTH, ST7796_TFT_HEIGHT);
}

void display_st7796_update()  {}
void display_st7796_deinit()  { bl_set(0); }
void display_st7796_set_brightness(uint8_t v) { bl_set(v); }
void display_st7796_set_text_colors(uint16_t, uint16_t bg) { _text_bg = bg; }
void display_st7796_clear() { display_st7796_fill_rect(0, 0, ST7796_TFT_WIDTH, ST7796_TFT_HEIGHT, _text_bg); }
void display_st7796_text(uint8_t, const char*) {}
void display_st7796_draw_string(int16_t, int16_t, const char*, uint16_t, uint16_t, uint8_t) {}

void display_st7796_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!s_panel || w <= 0 || h <= 0) return;
    static uint16_t row[480];
    int fill = (w < 480) ? w : 480;
    uint16_t swapped = (color >> 8) | (color << 8);
    for (int i = 0; i < fill; i++) row[i] = swapped;
    for (int r = 0; r < h; r++)
        esp_lcd_panel_draw_bitmap(s_panel, x, y + r, x + w, y + r + 1, row);
}

void display_st7796_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t c) {
    display_st7796_fill_rect(x, y, w, 1, c);
}

#endif // PURR_DISPLAY_ST7796
