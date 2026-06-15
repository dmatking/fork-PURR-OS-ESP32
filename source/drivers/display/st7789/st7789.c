// st7789.c — ST7789 SPI display driver for PURR OS using esp_lcd
//
// Uses ESP-IDF's built-in esp_lcd component for reliable SPI DMA transfers.
// Works with LV_COLOR_16_SWAP=y in sdkconfig (bytes already big-endian from LVGL).
//
// Target hardware: LilyGO T-Deck / T-Deck Plus — ST7789 320×240 landscape
//   Default pins: MOSI=41 SCLK=40 CS=12 DC=11 RST=-1 BL=42
//   Rotation:  swap_xy=true, mirror_x=true, mirror_y=true → MADCTL 0x70

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../kernel/core/purr_module.h"
#include "../../kernel/core/purr_kernel.h"
#include "../../kernel/catcalls/catcall_display.h"

static const char *TAG = "drv:st7789";

// ── Default pins (T-Deck / T-Deck Plus) ──────────────────────────────────────
#ifndef CONFIG_DRV_DISPLAY_CS_PIN
#  define CONFIG_DRV_DISPLAY_CS_PIN   12
#endif
#ifndef CONFIG_DRV_DISPLAY_DC_PIN
#  define CONFIG_DRV_DISPLAY_DC_PIN   11
#endif
#ifndef CONFIG_DRV_DISPLAY_MOSI_PIN
#  define CONFIG_DRV_DISPLAY_MOSI_PIN 41
#endif
#ifndef CONFIG_DRV_DISPLAY_SCLK_PIN
#  define CONFIG_DRV_DISPLAY_SCLK_PIN 40
#endif
#ifndef CONFIG_DRV_DISPLAY_RST_PIN
#  define CONFIG_DRV_DISPLAY_RST_PIN  (-1)
#endif
#ifndef CONFIG_DRV_DISPLAY_BL_PIN
#  define CONFIG_DRV_DISPLAY_BL_PIN   42
#endif

#define ST7789_WIDTH    320
#define ST7789_HEIGHT   240
#define ST7789_SPI_HOST SPI2_HOST
#define ST7789_CLK_HZ   (80 * 1000 * 1000)

// LEDC backlight
#define BL_SPEED_MODE  LEDC_LOW_SPEED_MODE
#define BL_TIMER_NUM   LEDC_TIMER_2
#define BL_CHANNEL     LEDC_CHANNEL_2
#define BL_FREQ_HZ     5000
#define BL_RESOLUTION  LEDC_TIMER_8_BIT

// ── Driver state ──────────────────────────────────────────────────────────────

typedef struct {
    int cs_pin;
    int dc_pin;
    int mosi_pin;
    int sclk_pin;
    int rst_pin;
    int bl_pin;
} st7789_pins_t;

static st7789_pins_t s_pins = {
    .cs_pin   = CONFIG_DRV_DISPLAY_CS_PIN,
    .dc_pin   = CONFIG_DRV_DISPLAY_DC_PIN,
    .mosi_pin = CONFIG_DRV_DISPLAY_MOSI_PIN,
    .sclk_pin = CONFIG_DRV_DISPLAY_SCLK_PIN,
    .rst_pin  = CONFIG_DRV_DISPLAY_RST_PIN,
    .bl_pin   = CONFIG_DRV_DISPLAY_BL_PIN,
};

static esp_lcd_panel_handle_t s_panel     = NULL;
static esp_lcd_panel_io_handle_t s_io     = NULL;
static bool                   s_ready     = false;
static uint16_t               s_width     = ST7789_WIDTH;
static uint16_t               s_height    = ST7789_HEIGHT;

// ── Public configure API ──────────────────────────────────────────────────────

void st7789_configure(int cs, int dc, int mosi, int sclk, int rst, int bl)
{
    s_pins.cs_pin   = cs;
    s_pins.dc_pin   = dc;
    s_pins.mosi_pin = mosi;
    s_pins.sclk_pin = sclk;
    s_pins.rst_pin  = rst;
    s_pins.bl_pin   = bl;
}

// ── Backlight (LEDC PWM) ──────────────────────────────────────────────────────

static void bl_init(int pin)
{
    ledc_timer_config_t tcfg = {
        .speed_mode      = BL_SPEED_MODE,
        .duty_resolution = BL_RESOLUTION,
        .timer_num       = BL_TIMER_NUM,
        .freq_hz         = BL_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tcfg);

    ledc_channel_config_t ccfg = {
        .gpio_num   = pin,
        .speed_mode = BL_SPEED_MODE,
        .channel    = BL_CHANNEL,
        .timer_sel  = BL_TIMER_NUM,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ccfg);
}

static void bl_set(uint8_t level)
{
    ledc_set_duty(BL_SPEED_MODE, BL_CHANNEL, level);
    ledc_update_duty(BL_SPEED_MODE, BL_CHANNEL);
}

// ── Screen fill ──────────────────────────────────────────────────────────────
// Used internally during hw_init (before s_ready) and publicly after init.
// Fills in 16-row DMA bands to stay well under the DMA-capable heap limit.

static void st7789_fill_gram(uint16_t color)
{
    if (!s_panel) return;
    uint16_t sw = (uint16_t)((color >> 8) | (color << 8)); // SPI big-endian
    const int ROWS = 16;
    uint16_t *buf = heap_caps_malloc(ST7789_WIDTH * ROWS * 2,
                                     MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf) return;
    for (int i = 0; i < ST7789_WIDTH * ROWS; i++) buf[i] = sw;
    for (int y = 0; y < ST7789_HEIGHT; y += ROWS) {
        int h = (y + ROWS <= ST7789_HEIGHT) ? ROWS : (ST7789_HEIGHT - y);
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, ST7789_WIDTH, y + h, buf);
    }
    free(buf);
}

void st7789_fill_screen(uint16_t color)
{
    if (!s_ready) return;
    st7789_fill_gram(color);
}

// ── esp_lcd init ──────────────────────────────────────────────────────────────

static esp_err_t st7789_hw_init(void)
{
    // SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = s_pins.mosi_pin,
        .miso_io_num     = -1,
        .sclk_io_num     = s_pins.sclk_pin,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = ST7789_WIDTH * ST7789_HEIGHT * 2 + 8,
    };
    esp_err_t ret = spi_bus_initialize(ST7789_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(ret));
        return ret;
    }

    // Panel IO (SPI)
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = s_pins.dc_pin,
        .cs_gpio_num       = s_pins.cs_pin,
        .pclk_hz           = ST7789_CLK_HZ,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ST7789_SPI_HOST, &io_cfg, &s_io),
        TAG, "panel io create failed");

    // ST7789 panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = s_pins.rst_pin,
        .rgb_endian     = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel),
        TAG, "panel create failed");

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

    // 320x240 landscape: swap XY + mirror X only = MADCTL 0x60 (MX|MV)
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, true, false);
    // Color inversion required for this panel variant
    esp_lcd_panel_invert_color(s_panel, true);

    // Clear GRAM to black before enabling display — prevents boot garbage.
    // Display off → fill → display on → backlight, so user never sees garbage.
    esp_lcd_panel_disp_on_off(s_panel, false);
    st7789_fill_gram(0x0000);
    esp_lcd_panel_disp_on_off(s_panel, true);

    // Backlight on after GRAM is clean
    if (s_pins.bl_pin >= 0) {
        bl_init(s_pins.bl_pin);
        bl_set(255);
    }

    s_width  = ST7789_WIDTH;
    s_height = ST7789_HEIGHT;
    s_ready  = true;

    ESP_LOGI(TAG, "ST7789 ready %dx%d (esp_lcd)", s_width, s_height);
    return ESP_OK;
}

// ── Catcall interface ─────────────────────────────────────────────────────────

static esp_err_t st7789_init(const display_config_t *cfg)
{
    if (s_ready) return ESP_OK;
    if (cfg && cfg->backlight_pin != 0) s_pins.bl_pin = (int)cfg->backlight_pin;
    return st7789_hw_init();
}

static esp_err_t st7789_push_pixels(int x, int y, int w, int h, const uint16_t *data)
{
    if (!s_ready || !data || w <= 0 || h <= 0) return ESP_ERR_INVALID_STATE;
    // esp_lcd_panel_draw_bitmap(x1, y1, x2_exclusive, y2_exclusive, data)
    // Data must already be in the correct byte order (big-endian for ST7789).
    // Set CONFIG_LV_COLOR_16_SWAP=y so LVGL outputs bytes correctly.
    return esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, data);
}

static esp_err_t st7789_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!s_ready || w <= 0 || h <= 0) return ESP_ERR_INVALID_STATE;
    // Swap bytes for big-endian SPI
    uint16_t swapped = (uint16_t)((color >> 8) | (color << 8));
    int total = w * h;
    uint16_t *buf = heap_caps_malloc((size_t)(total * 2), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf) return ESP_ERR_NO_MEM;
    for (int i = 0; i < total; i++) buf[i] = swapped;
    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, buf);
    free(buf);
    return ret;
}

static esp_err_t st7789_set_brightness(uint8_t level)
{
    if (s_pins.bl_pin < 0) return ESP_ERR_NOT_SUPPORTED;
    bl_set(level);
    return ESP_OK;
}

static void st7789_get_info(display_info_t *out)
{
    if (!out) return;
    out->width          = s_width;
    out->height         = s_height;
    out->bits_per_pixel = 16;
    strncpy(out->name, "ST7789", sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
}

static esp_err_t st7789_deinit(void)
{
    if (!s_ready) return ESP_OK;
    if (s_pins.bl_pin >= 0) bl_set(0);
    esp_lcd_panel_del(s_panel);
    esp_lcd_panel_io_del(s_io);
    spi_bus_free(ST7789_SPI_HOST);
    s_panel = NULL;
    s_io    = NULL;
    s_ready = false;
    return ESP_OK;
}

// ── Catcall descriptor ────────────────────────────────────────────────────────

static const catcall_display_t s_catcall = {
    .name            = "st7789",
    .catcall_version = CATCALL_DISPLAY_VERSION,
    .init            = st7789_init,
    .push_pixels     = st7789_push_pixels,
    .fill_rect       = st7789_fill_rect,
    .set_brightness  = st7789_set_brightness,
    .get_info        = st7789_get_info,
    .deinit          = st7789_deinit,
};

// ── Module lifecycle ──────────────────────────────────────────────────────────

int st7789_drv_init(void)
{
    esp_err_t ret = st7789_hw_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "hardware init failed: %s", esp_err_to_name(ret));
        return -1;
    }
    purr_kernel_register_display(&s_catcall);
    return 0;
}

static void st7789_drv_deinit(void)
{
    st7789_deinit();
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(st7789) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_DRIVER,
    .load_priority     = PURR_PRIORITY_REQUIRED,
    .name              = "st7789",
    .version           = "2.0.0",
    .kernel_min        = "0.9.0",
    .kernel_max        = "",
    .provided_catcalls = CATCALL_FLAG_DISPLAY,
    .required_catcalls = 0,
    .init              = st7789_drv_init,
    .deinit            = st7789_drv_deinit,
};
