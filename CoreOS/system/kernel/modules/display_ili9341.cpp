// display_ili9341.cpp — ILI9341 display driver (ESP-IDF)

#include "display_ili9341.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "display";
static spi_device_handle_t _spi = NULL;

static void _write_cmd(uint8_t cmd) {
    gpio_set_level((gpio_num_t)CYD_TFT_DC, 0);
    spi_transaction_t tx = {};
    tx.tx_buffer = &cmd;
    tx.length = 8;
    spi_device_transmit(_spi, &tx);
}

static void _write_data(uint8_t data) {
    gpio_set_level((gpio_num_t)CYD_TFT_DC, 1);
    spi_transaction_t tx = {};
    tx.tx_buffer = &data;
    tx.length = 8;
    spi_device_transmit(_spi, &tx);
}

void display_ili9341_init() {
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = CYD_TFT_MOSI;
    bus_cfg.miso_io_num = -1;
    bus_cfg.sclk_io_num = CYD_TFT_SCK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 320 * 240 * 2;
    bus_cfg.flags = SPICOMMON_BUSFLAG_MASTER;
    spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.mode = 0;
    dev_cfg.clock_speed_hz = 40000000;
    dev_cfg.spics_io_num = CYD_TFT_CS;
    dev_cfg.queue_size = 7;
    spi_bus_add_device(SPI2_HOST, &dev_cfg, &_spi);

    gpio_set_direction((gpio_num_t)CYD_TFT_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)CYD_TFT_RST, GPIO_MODE_OUTPUT);

    // Reset sequence
    gpio_set_level((gpio_num_t)CYD_TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level((gpio_num_t)CYD_TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)CYD_TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    // Standard ILI9341 init
    _write_cmd(0xCF);
    _write_data(0x00); _write_data(0x83); _write_data(0x30);

    _write_cmd(0xED);
    _write_data(0x64); _write_data(0x03); _write_data(0x12); _write_data(0x81);

    _write_cmd(0xE8);
    _write_data(0x85); _write_data(0x01); _write_data(0x79);

    _write_cmd(0xCB);
    _write_data(0x39); _write_data(0x2C); _write_data(0x00); _write_data(0x34); _write_data(0x02);

    _write_cmd(0xF7);
    _write_data(0x20);

    _write_cmd(0xEA);
    _write_data(0x00); _write_data(0x00);

    _write_cmd(0xC0); _write_data(0x26);
    _write_cmd(0xC1); _write_data(0x11);
    _write_cmd(0xC5); _write_data(0x35); _write_data(0x3E);
    _write_cmd(0xC7); _write_data(0x9E);

    _write_cmd(0x36); _write_data(0x48);  // landscape, BGR
    _write_cmd(0x3A); _write_data(0x55);  // 16-bit/pixel

    _write_cmd(0xB1); _write_data(0x00); _write_data(0x1B);
    _write_cmd(0xF2); _write_data(0x08);
    _write_cmd(0x26); _write_data(0x01);

    _write_cmd(0xE0);  // Gamma +
    _write_data(0x1F); _write_data(0x1A); _write_data(0x18); _write_data(0x0A);
    _write_data(0x0F); _write_data(0x06); _write_data(0x45); _write_data(0x87);
    _write_data(0x32); _write_data(0x0A); _write_data(0x07); _write_data(0x02);
    _write_data(0x07); _write_data(0x05); _write_data(0x00);

    _write_cmd(0xE1);  // Gamma -
    _write_data(0x00); _write_data(0x25); _write_data(0x27); _write_data(0x05);
    _write_data(0x10); _write_data(0x09); _write_data(0x3A); _write_data(0x78);
    _write_data(0x4D); _write_data(0x05); _write_data(0x18); _write_data(0x0D);
    _write_data(0x38); _write_data(0x3A); _write_data(0x1F);

    _write_cmd(0x11);  // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));

    _write_cmd(0x29);  // Display on
    vTaskDelay(pdMS_TO_TICKS(120));

    // Backlight PWM
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.timer_num = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num = CYD_TFT_BL;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel = LEDC_CHANNEL_0;
    ch_cfg.timer_sel = LEDC_TIMER_0;
    ch_cfg.duty = 255;
    ledc_channel_config(&ch_cfg);

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

    ESP_LOGI(TAG, "ILI9341 initialized");
}

void display_ili9341_update() {}
void display_ili9341_deinit() {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void display_ili9341_set_brightness(uint8_t level) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void display_ili9341_clear() {}
void display_ili9341_text(uint8_t row, const char* text) { (void)row; (void)text; }
void display_ili9341_set_text_colors(uint16_t fg, uint16_t bg) { (void)fg; (void)bg; }
void display_ili9341_push_block(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void display_ili9341_push_colors(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* colors) {
    (void)x; (void)y; (void)w; (void)h; (void)colors;
}
void display_ili9341_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void display_ili9341_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    (void)x; (void)y; (void)w; (void)color;
}
void display_ili9341_draw_string(int16_t x, int16_t y, const char* s, uint16_t fg, uint16_t bg, uint8_t size) {
    (void)x; (void)y; (void)s; (void)fg; (void)bg; (void)size;
}

#ifdef PURR_HAS_LVGL
void display_ili9341_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    _write_cmd(0x2A);  // Column
    _write_data(area->x1 >> 8);
    _write_data(area->x1 & 0xFF);
    _write_data(area->x2 >> 8);
    _write_data(area->x2 & 0xFF);

    _write_cmd(0x2B);  // Row
    _write_data(area->y1 >> 8);
    _write_data(area->y1 & 0xFF);
    _write_data(area->y2 >> 8);
    _write_data(area->y2 & 0xFF);

    _write_cmd(0x2C);  // Write
    gpio_set_level((gpio_num_t)CYD_TFT_DC, 1);

    spi_transaction_t tx = {};
    tx.tx_buffer = color_p;
    tx.length = w * h * 16;
    spi_device_transmit(_spi, &tx);

    lv_disp_flush_ready(drv);
}
#endif
