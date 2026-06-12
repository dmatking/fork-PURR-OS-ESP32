// display_ili9341.cpp — ILI9341 driver, pure ESP-IDF, fully synchronous (no DMA race)
// Uses spi_device_transmit() which blocks until each transfer completes.
//
// Hardware: ESP32-2432S028R — ILI9341 2.8" 320x240 on HSPI (SPI2)
//   MOSI=13  MISO=12  SCLK=14  CS=15  DC=2  RST=4  BL=21
// Variant: ESP32-2432S024C — same SPI bus, BL=27, RST=-1

#include "display_ili9341.h"
#include "display_font5x7.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "ili9341";

// ── Pin map ───────────────────────────────────────────────────────────────────
// S024C: BL=27, no RST pin. S028R: BL=21, GPIO4 is also RGB-RED LED — don't toggle it.
#ifdef CYD_VARIANT_S024C
#  define LCD_BL     27
#else
#  define LCD_BL     21
#endif
#define LCD_RST    (-1)
#define LCD_HOST     SPI2_HOST
#define LCD_MOSI     13
#define LCD_MISO     12
#define LCD_SCLK     14
#define LCD_CS       15
#define LCD_DC       2
#define LCD_CLK_HZ   (40 * 1000 * 1000)

// ── LEDC backlight ────────────────────────────────────────────────────────────
#define BL_MODE      LEDC_LOW_SPEED_MODE
#define BL_TIMER     LEDC_TIMER_1
#define BL_CHAN      LEDC_CHANNEL_1

// ── ILI9341 register addresses ────────────────────────────────────────────────
#define ILI_SWRESET  0x01
#define ILI_SLPOUT   0x11
#define ILI_NORON    0x13
#define ILI_DISPON   0x29
#define ILI_CASET    0x2A
#define ILI_PASET    0x2B
#define ILI_RAMWR    0x2C
#define ILI_MADCTL   0x36
#define ILI_COLMOD   0x3A
#define ILI_FRMCTR1  0xB1
#define ILI_DFUNCTR  0xB6
#define ILI_PWCTR1   0xC0
#define ILI_PWCTR2   0xC1
#define ILI_VMCTR1   0xC5
#define ILI_VMCTR2   0xC7
#define ILI_GMCTRP1  0xE0
#define ILI_GMCTRN1  0xE1

// MADCTL: MV=1 MX=1 BGR=1 → 320×240 landscape, correct colour order
#define MADCTL_VAL   0x68

// ── State ─────────────────────────────────────────────────────────────────────
static spi_device_handle_t s_spi   = NULL;
static bool                s_ready = false;

// ── Low-level SPI helpers (fully synchronous / blocking) ─────────────────────

static void spi_cmd(uint8_t cmd) {
    gpio_set_level((gpio_num_t)LCD_DC, 0);   // DC low = command
    spi_transaction_t t = {};
    t.length    = 8;
    t.tx_buffer = &cmd;
    t.flags     = 0;
    spi_device_transmit(s_spi, &t);
}

static void spi_data(const void* data, size_t len) {
    if (!len) return;
    gpio_set_level((gpio_num_t)LCD_DC, 1);   // DC high = data
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = data;
    t.flags     = 0;
    spi_device_transmit(s_spi, &t);
}

static void spi_dat1(uint8_t d) { spi_data(&d, 1); }

static void lcd_write_cmd_data(uint8_t cmd, const uint8_t* d, size_t n) {
    spi_cmd(cmd);
    if (n) spi_data(d, n);
}

// ── ILI9341 init sequence ─────────────────────────────────────────────────────
// Matches TFT_eSPI ILI9341_Init.h — vendor init commands required by CYD panels.
static void ili9341_init_regs(void) {
    // Hardware reset (if RST pin present) is done before calling this function.
    // Soft-reset then wait for panel to come out of sleep.
    spi_cmd(ILI_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Vendor-specific initialisation — undocumented but required by CYD panels.
    { static const uint8_t d[] = {0x03,0x80,0x02}; lcd_write_cmd_data(0xEF, d, 3); }
    { static const uint8_t d[] = {0x00,0xC1,0x30}; lcd_write_cmd_data(0xCF, d, 3); }
    { static const uint8_t d[] = {0x64,0x03,0x12,0x81}; lcd_write_cmd_data(0xED, d, 4); }
    { static const uint8_t d[] = {0x85,0x00,0x78}; lcd_write_cmd_data(0xE8, d, 3); }
    { static const uint8_t d[] = {0x39,0x2C,0x00,0x34,0x02}; lcd_write_cmd_data(0xCB, d, 5); }
    { static const uint8_t d[] = {0x20}; lcd_write_cmd_data(0xF7, d, 1); }
    { static const uint8_t d[] = {0x00,0x00}; lcd_write_cmd_data(0xEA, d, 2); }

    spi_cmd(ILI_PWCTR1); spi_dat1(0x23);
    spi_cmd(ILI_PWCTR2); spi_dat1(0x10);
    { static const uint8_t d[] = {0x3E,0x28}; lcd_write_cmd_data(ILI_VMCTR1, d, 2); }
    spi_cmd(ILI_VMCTR2); spi_dat1(0x86);
    spi_cmd(ILI_MADCTL); spi_dat1(MADCTL_VAL);
    spi_cmd(ILI_COLMOD); spi_dat1(0x55);   // 16bpp RGB565
    { static const uint8_t d[] = {0x00,0x13}; lcd_write_cmd_data(ILI_FRMCTR1, d, 2); }  // 100 Hz
    { static const uint8_t d[] = {0x08,0x82,0x27}; lcd_write_cmd_data(ILI_DFUNCTR, d, 3); }
    spi_cmd(0xF2); spi_dat1(0x00);          // 3Gamma disable
    spi_cmd(0x26); spi_dat1(0x01);          // Gamma curve 1

    { static const uint8_t d[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
      lcd_write_cmd_data(ILI_GMCTRP1, d, 15); }
    { static const uint8_t d[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
      lcd_write_cmd_data(ILI_GMCTRN1, d, 15); }

    spi_cmd(ILI_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
    // DISPON sent after GRAM is cleared in display_ili9341_init()
}

// ── Set address window ────────────────────────────────────────────────────────
static void set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t col[] = { (uint8_t)(x0>>8),(uint8_t)x0, (uint8_t)(x1>>8),(uint8_t)x1 };
    uint8_t row[] = { (uint8_t)(y0>>8),(uint8_t)y0, (uint8_t)(y1>>8),(uint8_t)y1 };
    lcd_write_cmd_data(ILI_CASET, col, 4);
    lcd_write_cmd_data(ILI_PASET, row, 4);
    spi_cmd(ILI_RAMWR);
}

// ── Backlight ─────────────────────────────────────────────────────────────────
static void bl_init(void) {
    ledc_timer_config_t t = {};
    t.speed_mode       = BL_MODE;
    t.duty_resolution  = LEDC_TIMER_8_BIT;
    t.timer_num        = BL_TIMER;
    t.freq_hz          = 5000;
    t.clk_cfg          = LEDC_AUTO_CLK;
    ledc_timer_config(&t);

    ledc_channel_config_t ch = {};
    ch.gpio_num  = LCD_BL;
    ch.speed_mode = BL_MODE;
    ch.channel   = BL_CHAN;
    ch.timer_sel = BL_TIMER;
    ch.duty      = 0;
    ch.hpoint    = 0;
    ledc_channel_config(&ch);
}

static void bl_set(uint8_t v) {
    ledc_set_duty(BL_MODE, BL_CHAN, v);
    ledc_update_duty(BL_MODE, BL_CHAN);
}

// ── Public init ───────────────────────────────────────────────────────────────
void display_ili9341_init(void) {
    if (s_ready) return;

    // DC pin as output
    gpio_config_t dc = {};
    dc.pin_bit_mask = 1ULL << LCD_DC;
    dc.mode         = GPIO_MODE_OUTPUT;
    gpio_config(&dc);
    gpio_set_level((gpio_num_t)LCD_DC, 1);

#if LCD_RST >= 0
    gpio_config_t rst = {};
    rst.pin_bit_mask = 1ULL << LCD_RST;
    rst.mode         = GPIO_MODE_OUTPUT;
    gpio_config(&rst);
    gpio_set_level((gpio_num_t)LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
#endif

    spi_bus_config_t bus = {};
    bus.mosi_io_num   = LCD_MOSI;
    bus.miso_io_num   = LCD_MISO;
    bus.sclk_io_num   = LCD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = CYD_TFT_WIDTH * CYD_TFT_HEIGHT * 2 + 8;
    spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = LCD_CLK_HZ;
    dev.mode           = 0;
    dev.spics_io_num   = LCD_CS;
    dev.queue_size     = 1;
    spi_bus_add_device(LCD_HOST, &dev, &s_spi);

    ili9341_init_regs();

    s_ready = true;  // must be set before fill_rect guard fires
    display_ili9341_fill_rect(0, 0, CYD_TFT_WIDTH, CYD_TFT_HEIGHT, 0x0000);
    spi_cmd(ILI_DISPON);  // turn on display only after GRAM is cleared

    bl_init();
    bl_set(255);
    ESP_LOGI(TAG, "ILI9341 ready %dx%d", CYD_TFT_WIDTH, CYD_TFT_HEIGHT);
}

void display_ili9341_tick(void)               {}
void display_ili9341_deinit(void)               { bl_set(0); }
void display_ili9341_set_brightness(uint8_t v)  { bl_set(v); }
void display_ili9341_clear(void) {
    display_ili9341_fill_rect(0, 0, CYD_TFT_WIDTH, CYD_TFT_HEIGHT, 0x0000);
}
void display_ili9341_text(uint8_t, const char*)           {}
void display_ili9341_set_text_colors(uint16_t, uint16_t)  {}

// ── Drawing primitives ────────────────────────────────────────────────────────

// row buffer — 320 pixels × 2 bytes, reused across fill calls.
static uint16_t s_row[320];

void display_ili9341_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!s_ready || w <= 0 || h <= 0) return;
    int cols = (w <= 320) ? w : 320;
    // byte-swap for big-endian SPI
    uint16_t sw = (uint16_t)((color >> 8) | (color << 8));
    for (int i = 0; i < cols; i++) s_row[i] = sw;

    set_addr_window((uint16_t)x, (uint16_t)y,
                    (uint16_t)(x + cols - 1), (uint16_t)(y + h - 1));
    // DC already set to command by set_addr_window (ends on RAMWR cmd).
    // Switch to data and blast pixels row by row.
    for (int r = 0; r < h; r++)
        spi_data(s_row, cols * 2);
}

void display_ili9341_push_block(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    display_ili9341_fill_rect(x, y, w, h, color);
}

void display_ili9341_push_colors(int16_t x, int16_t y, int16_t w, int16_t h,
                                  const uint16_t* colors) {
    if (!s_ready || !colors || w <= 0 || h <= 0) return;
    int cols = (w <= 320) ? w : 320;
    set_addr_window((uint16_t)x, (uint16_t)y,
                    (uint16_t)(x + cols - 1), (uint16_t)(y + h - 1));
    for (int r = 0; r < h; r++) {
        const uint16_t* src = colors + r * w;
        for (int i = 0; i < cols; i++)
            s_row[i] = (uint16_t)((src[i] >> 8) | (src[i] << 8));
        spi_data(s_row, cols * 2);
    }
}

void display_ili9341_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    display_ili9341_fill_rect(x, y, w, 1, color);
}

void display_ili9341_draw_string(int16_t x, int16_t y, const char* s,
                                  uint16_t fg, uint16_t bg, uint8_t size) {
    display_font5x7_draw_string(x, y, s, fg, bg, size, display_ili9341_fill_rect);
}

#include "purr_sys_drv.h"
static sys_drv_t s_ili9341_drv = {
    .name="display:ili9341",.subsystem="display",.enabled=false,
    .init=display_ili9341_init,.tick=display_ili9341_tick,.deinit=display_ili9341_deinit,.cmd=NULL
};
void display_ili9341_drv_register(bool enabled){s_ili9341_drv.enabled=enabled;sys_drv_register(&s_ili9341_drv);}
