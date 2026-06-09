// T-Deck Plus ST7789 HAL — embedded inline (pins differ from Waveshare ST7789 driver).
// GPIO 10 is the peripheral power enable and must be driven HIGH before SPI init.

#include "hal/hal_lcd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#define TDECK_LCD_WIDTH   320
#define TDECK_LCD_HEIGHT  240

#define LCD_HOST    SPI3_HOST
#define LCD_MOSI    41
#define LCD_MISO    38
#define LCD_SCLK    40
#define LCD_CS      12
#define LCD_DC      11
#define LCD_RST     (-1)
#define LCD_BL      42
#define LCD_PWR_EN  10
#define LCD_CLK_HZ  (40 * 1000 * 1000)

#define BL_MODE     LEDC_LOW_SPEED_MODE
#define BL_TIMER    LEDC_TIMER_1
#define BL_CHANNEL  LEDC_CHANNEL_1

static const char* TAG = "tdeck_lcd";

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io    = NULL;
static bool s_ready = false;

static void bl_init(void) {
    ledc_timer_config_t t = { .speed_mode = BL_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                               .timer_num = BL_TIMER, .freq_hz = 5000,
                               .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&t);
    ledc_channel_config_t ch = { .gpio_num = LCD_BL, .speed_mode = BL_MODE,
                                  .channel = BL_CHANNEL, .timer_sel = BL_TIMER, .duty = 0 };
    ledc_channel_config(&ch);
}

static void bl_set(uint8_t v) {
    ledc_set_duty(BL_MODE, BL_CHANNEL, v);
    ledc_update_duty(BL_MODE, BL_CHANNEL);
}

static inline uint16_t to_rgb565(mw_hal_lcd_colour_t c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >>  8) & 0xFF;
    uint8_t b = (c      ) & 0xFF;
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

extern "C" {

void mw_hal_lcd_init(void) {
    if (s_ready) return;
    s_ready = true;

    // Power enable — must be HIGH before driving SPI peripherals
    gpio_set_direction((gpio_num_t)LCD_PWR_EN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)LCD_PWR_EN, 1);

    spi_bus_config_t bus = { .mosi_io_num = LCD_MOSI, .miso_io_num = LCD_MISO,
                              .sclk_io_num = LCD_SCLK, .quadwp_io_num = -1,
                              .quadhd_io_num = -1,
                              .max_transfer_sz = TDECK_LCD_WIDTH * TDECK_LCD_HEIGHT * 2 };
    spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_spi_config_t io_cfg = { .cs_gpio_num = LCD_CS, .dc_gpio_num = LCD_DC,
                                              .pclk_hz = LCD_CLK_HZ,
                                              .trans_queue_depth = 10,
                                              .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
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
    ESP_LOGI(TAG, "OK %dx%d  pwr_en=%d", TDECK_LCD_WIDTH, TDECK_LCD_HEIGHT, LCD_PWR_EN);
}

int16_t mw_hal_lcd_get_display_width(void)  { return TDECK_LCD_WIDTH;  }
int16_t mw_hal_lcd_get_display_height(void) { return TDECK_LCD_HEIGHT; }

void mw_hal_lcd_pixel(int16_t x, int16_t y, mw_hal_lcd_colour_t colour) {
    if (!s_panel) return;
    uint16_t sw = to_rgb565(colour);
    sw = (uint16_t)((sw >> 8) | (sw << 8));
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + 1, y + 1, &sw);
}

void mw_hal_lcd_filled_rectangle(int16_t sx, int16_t sy, int16_t w, int16_t h,
                                   mw_hal_lcd_colour_t colour) {
    if (!s_panel || w <= 0 || h <= 0) return;
    static uint16_t row[320];
    uint16_t sw = to_rgb565(colour);
    sw = (uint16_t)((sw >> 8) | (sw << 8));
    int fill = (w <= 320) ? w : 320;
    for (int i = 0; i < fill; i++) row[i] = sw;
    for (int r = 0; r < h; r++)
        esp_lcd_panel_draw_bitmap(s_panel, sx, sy + r, sx + w, sy + r + 1, row);
}

void mw_hal_lcd_monochrome_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                        uint16_t bitmap_width, uint16_t bitmap_height,
                                        int16_t clip_start_x, int16_t clip_start_y,
                                        int16_t clip_width, int16_t clip_height,
                                        mw_hal_lcd_colour_t fg_colour,
                                        mw_hal_lcd_colour_t bg_colour,
                                        const uint8_t *image_data) {
    if (!s_panel) return;
    uint16_t fg = to_rgb565(fg_colour); fg = (uint16_t)((fg >> 8) | (fg << 8));
    uint16_t bg = to_rgb565(bg_colour); bg = (uint16_t)((bg >> 8) | (bg << 8));
    uint16_t stride = (bitmap_width + 7) / 8;

    static uint16_t row_buf[320];
    for (int16_t row = 0; row < (int16_t)bitmap_height; row++) {
        int16_t sy = image_start_y + row;
        if (sy < clip_start_y || sy >= clip_start_y + clip_height) continue;

        int16_t x0 = (image_start_x >= clip_start_x) ? 0 : (clip_start_x - image_start_x);
        int16_t x1 = (int16_t)bitmap_width;
        if (image_start_x + x1 > clip_start_x + clip_width)
            x1 = clip_start_x + clip_width - image_start_x;
        if (x0 >= x1) continue;

        for (int16_t col = x0; col < x1; col++) {
            bool set = (image_data[row * stride + col / 8] >> (7 - (col & 7))) & 1;
            row_buf[col - x0] = set ? fg : bg;
        }
        esp_lcd_panel_draw_bitmap(s_panel,
            image_start_x + x0, sy,
            image_start_x + x1, sy + 1, row_buf);
    }
}

void mw_hal_lcd_colour_bitmap_clip(int16_t image_start_x, int16_t image_start_y,
                                    uint16_t bitmap_width, uint16_t bitmap_height,
                                    int16_t clip_start_x, int16_t clip_start_y,
                                    int16_t clip_width, int16_t clip_height,
                                    const uint8_t *image_data) {
    if (!s_panel) return;
    static uint16_t row_buf[320];
    for (int16_t row = 0; row < (int16_t)bitmap_height; row++) {
        int16_t sy = image_start_y + row;
        if (sy < clip_start_y || sy >= clip_start_y + clip_height) continue;

        int16_t x0 = (image_start_x >= clip_start_x) ? 0 : (clip_start_x - image_start_x);
        int16_t x1 = (int16_t)bitmap_width;
        if (image_start_x + x1 > clip_start_x + clip_width)
            x1 = clip_start_x + clip_width - image_start_x;
        if (x0 >= x1) continue;

        for (int16_t col = x0; col < x1; col++) {
            const uint8_t *p = image_data + (row * bitmap_width + col) * 3;
            uint16_t px = ((uint16_t)(p[0] & 0xF8) << 8)
                        | ((uint16_t)(p[1] & 0xFC) << 3)
                        | (p[2] >> 3);
            row_buf[col - x0] = (uint16_t)((px >> 8) | (px << 8));
        }
        esp_lcd_panel_draw_bitmap(s_panel,
            image_start_x + x0, sy,
            image_start_x + x1, sy + 1, row_buf);
    }
}

} // extern "C"
