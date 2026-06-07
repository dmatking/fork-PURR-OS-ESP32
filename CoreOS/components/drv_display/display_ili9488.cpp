// display_ili9488.cpp — ILI9488 display driver via esp_lcd + LVGL (pure ESP-IDF)
// Target: CattoPad 3.5" 320x480

#include "display_ili9488.h"

#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include <lvgl.h>
#include <string.h>

static const char* TAG = "ili9488";

// CattoPad wiring
#define LCD_HOST    SPI3_HOST
#define LCD_MOSI    11
#define LCD_SCLK    12
#define LCD_MISO    -1
#define LCD_CS      10
#define LCD_DC       9
#define LCD_RST      8
#define LCD_CLK_HZ  (40 * 1000 * 1000)

#define BL_MODE     LEDC_LOW_SPEED_MODE
#define BL_TIMER    LEDC_TIMER_3
#define BL_CHANNEL  LEDC_CHANNEL_3

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io    = NULL;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[TFT_WIDTH * 10];
static lv_color_t buf2[TFT_WIDTH * 10];

static void bl_init(void) {
    ledc_timer_config_t t = { .speed_mode = BL_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                               .timer_num = BL_TIMER, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&t);
    ledc_channel_config_t ch = { .gpio_num = TFT_BL, .speed_mode = BL_MODE,
                                  .channel = BL_CHANNEL, .timer_sel = BL_TIMER, .duty = 0 };
    ledc_channel_config(&ch);
}
static void bl_set(uint8_t v) { ledc_set_duty(BL_MODE, BL_CHANNEL, v); ledc_update_duty(BL_MODE, BL_CHANNEL); }

void display_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    uint16_t* src = (uint16_t*)color_p;
    int total = w * h;
    for (int i = 0; i < total; i++) src[i] = (src[i] >> 8) | (src[i] << 8);
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, src);
    lv_disp_flush_ready(drv);
}

void display_ili9488_init() {
    spi_bus_config_t bus = { .mosi_io_num = LCD_MOSI, .miso_io_num = LCD_MISO,
                              .sclk_io_num = LCD_SCLK, .quadwp_io_num = -1,
                              .quadhd_io_num = -1,
                              .max_transfer_sz = TFT_WIDTH * 10 * 2 };
    spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_spi_config_t io_cfg = { .cs_gpio_num = LCD_CS, .dc_gpio_num = LCD_DC,
                                              .spi_clock_hz = LCD_CLK_HZ,
                                              .lcd_cmd_bits = 8, .lcd_param_bits = 8,
                                              .trans_queue_depth = 10 };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io);

    // ILI9488 uses ST7789-compatible driver (both are ILI941-family)
    esp_lcd_panel_dev_config_t panel_cfg = { .reset_gpio_num = LCD_RST,
                                              .rgb_endian = LCD_RGB_ENDIAN_BGR,
                                              .bits_per_pixel = 16 };
    esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel);
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_disp_on_off(s_panel, true);

    bl_init(); bl_set(255);

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, TFT_WIDTH * 10);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = TFT_WIDTH;
    disp_drv.ver_res  = TFT_HEIGHT;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "OK %dx%d", TFT_WIDTH, TFT_HEIGHT);
}

void display_ili9488_update()  {}
void display_ili9488_deinit()  { bl_set(0); esp_lcd_panel_disp_on_off(s_panel, false); }
void display_ili9488_set_brightness(uint8_t v) { bl_set(v); }
