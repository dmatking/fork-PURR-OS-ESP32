#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include "initialisation.h"
#include "screens.h"

static lv_display_t *s_grid_disp;
static void on_grid_timer(lv_timer_t *timer) { numpad_screen_load(s_grid_disp); }

static const char *TAG = "jc3248w535en";

/* Defines mirrored from main.c */
#define LCD_HOST          SPI2_HOST
#define LCD_H_RES         320
#define LCD_V_RES         480
#define LCD_BIT_PER_PIXEL 16
#define PIN_LCD_CS        45
#define PIN_LCD_PCLK      47
#define PIN_LCD_D0        21
#define PIN_LCD_D1        48
#define PIN_LCD_D2        40
#define PIN_LCD_D3        39
#define PIN_LCD_RST       -1
#define PIN_LCD_BL         1
#define TOUCH_I2C_PORT    I2C_NUM_0
#define PIN_TOUCH_SDA      4
#define PIN_TOUCH_SCL      8
#define TOUCH_I2C_HZ      (400 * 1000)
#define LVGL_TICK_PERIOD_MS 2
#define BOUNCE_LINES      40

/* Globals defined in main.c */
extern esp_lcd_panel_handle_t    s_panel;
extern esp_lcd_panel_io_handle_t s_io;
extern esp_lcd_touch_handle_t    s_tp;
extern lv_display_t             *s_disp;
extern SemaphoreHandle_t         s_lvgl_mux;
extern uint16_t                 *s_bounce;
extern SemaphoreHandle_t         s_flush_done;


/* ---------------- Vendor init sequence (Guition AXS15231B) ---------------- */
/* Sourced from the JC3248W535-class vendor BSP (see esp-iot-solution#579). */
static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xBB, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5}, 8, 0},
    {0xA0, (uint8_t[]){0xC0,0x10,0x00,0x02,0x00,0x00,0x04,0x3F,0x20,0x05,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00}, 17, 0},
    {0xA2, (uint8_t[]){0x30,0x3C,0x24,0x14,0xD0,0x20,0xFF,0xE0,0x40,0x19,0x80,0x80,0x80,0x20,0xF9,0x10,0x02,0xFF,0xFF,0xF0,0x90,0x01,0x32,0xA0,0x91,0xE0,0x20,0x7F,0xFF,0x00,0x5A}, 31, 0},
    {0xD0, (uint8_t[]){0xE0,0x40,0x51,0x24,0x08,0x05,0x10,0x01,0x20,0x15,0x42,0xC2,0x22,0x22,0xAA,0x03,0x10,0x12,0x60,0x14,0x1E,0x51,0x15,0x00,0x8A,0x20,0x00,0x03,0x3A,0x12}, 30, 0},
    {0xA3, (uint8_t[]){0xA0,0x06,0xAA,0x00,0x08,0x02,0x0A,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x55,0x55}, 22, 0},
    {0xC1, (uint8_t[]){0x31,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,0x52,0x00,0x45,0x3B,0x0B,0x02,0x0D,0x00,0xFF,0x40}, 30, 0},
    {0xC3, (uint8_t[]){0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01}, 11, 0},
    {0xC4, (uint8_t[]){0x00,0x24,0x33,0x80,0x00,0xEA,0x64,0x32,0xC8,0x64,0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x00,0x00,0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50}, 29, 0},
    {0xC5, (uint8_t[]){0x18,0x00,0x00,0x03,0xFE,0x3A,0x4A,0x20,0x30,0x10,0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x3A,0x4A,0x20,0x10,0x10,0x00}, 23, 0},
    {0xC6, (uint8_t[]){0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,0x12,0x22,0x01,0x03,0x00,0x3F,0x6A,0x18,0xC8,0x22}, 20, 0},
    {0xC7, (uint8_t[]){0x50,0x32,0x28,0x00,0xA2,0x80,0x8F,0x00,0x80,0xFF,0x07,0x11,0x9C,0x67,0xFF,0x24,0x0C,0x0D,0x0E,0x0F}, 20, 0},
    {0xC9, (uint8_t[]){0x33,0x44,0x44,0x01}, 4, 0},
    {0xCF, (uint8_t[]){0x2C,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,0x88,0x00,0x65,0x09,0x22,0xC4,0x0C,0x77,0x22,0x44,0xAA,0x55,0x08,0x08,0x12,0xA0,0x08}, 27, 0},
    {0xD5, (uint8_t[]){0x40,0x8E,0x8D,0x01,0x35,0x04,0x92,0x74,0x04,0x92,0x74,0x04,0x08,0x6A,0x04,0x46,0x03,0x03,0x03,0x03,0x82,0x01,0x03,0x00,0xE0,0x51,0xA1,0x00,0x00,0x00}, 30, 0},
    {0xD6, (uint8_t[]){0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x93,0x00,0x01,0x83,0x07,0x07,0x00,0x07,0x07,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x84,0x00,0x20,0x01,0x00}, 30, 0},
    {0xD7, (uint8_t[]){0x03,0x01,0x0B,0x09,0x0F,0x0D,0x1E,0x1F,0x18,0x1D,0x1F,0x19,0x40,0x8E,0x04,0x00,0x20,0xA0,0x1F}, 19, 0},
    {0xD8, (uint8_t[]){0x02,0x00,0x0A,0x08,0x0E,0x0C,0x1E,0x1F,0x18,0x1D,0x1F,0x19}, 12, 0},
    {0xD9, (uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}, 12, 0},
    {0xDD, (uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}, 12, 0},
    {0xDF, (uint8_t[]){0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90}, 8, 0},
    {0xE0, (uint8_t[]){0x3B,0x28,0x10,0x16,0x0C,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x13,0x2C,0x33,0x28,0x0D}, 17, 0},
    {0xE1, (uint8_t[]){0x37,0x28,0x10,0x16,0x0B,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x14,0x2C,0x33,0x28,0x0F}, 17, 0},
    {0xE2, (uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D}, 17, 0},
    {0xE3, (uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F}, 17, 0},
    {0xE4, (uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D}, 17, 0},
    {0xE5, (uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0F}, 17, 0},
    {0xA4, (uint8_t[]){0x85,0x85,0x95,0x82,0xAF,0xAA,0xAA,0x80,0x10,0x30,0x40,0x40,0x20,0xFF,0x60,0x30}, 16, 0},
    {0xA4, (uint8_t[]){0x85,0x85,0x95,0x85}, 4, 0},
    {0xBB, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 8, 0},
    {0x13, (uint8_t[]){0x00}, 0, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},   /* sleep out, 120 ms */
    {0x29, (uint8_t[]){0x00}, 0, 10},    /* display on */
};

/* ---------------- LVGL flush ---------------- */

/* Fires from ISR when one DMA chunk to the panel finishes. */
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                          esp_lcd_panel_io_event_data_t *e,
                                          void *user_ctx)
{
    BaseType_t hpw = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &hpw);
    return hpw == pdTRUE;
}

/* In FULL render mode area is always (0,0)-(W-1,H-1) and px_map is one
 * full screen in PSRAM. We copy it line-strip by line-strip through the
 * internal-SRAM bounce buffer and ship each strip to the panel.
 *
 * The destination coordinates are always (0,0) anyway because the panel
 * ignores CASET/RASET — we still pass the correct ones, in case a
 * future firmware revision honors them. */
void lcd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    static uint32_t flush_count = 0;
    static uint32_t last_log = 0;
    flush_count++;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_log >= 1000) {
        ESP_LOGI(TAG, "flush rate: %u/s", (unsigned)flush_count);
        flush_count = 0;
        last_log = now;
    }

    for (int y = 0; y < LCD_V_RES; y += BOUNCE_LINES) {
        int y_end = y + BOUNCE_LINES;
        if (y_end > LCD_V_RES) y_end = LCD_V_RES;
        const size_t bytes = (size_t)LCD_H_RES * (y_end - y) * sizeof(uint16_t);

        uint16_t *src = (uint16_t *)(px_map + (size_t)y * LCD_H_RES * 2);
        size_t pixels_to_copy = bytes / sizeof(uint16_t);

        for (size_t i = 0; i < pixels_to_copy; i++) {
            s_bounce[i] = __builtin_bswap16(src[i]);
        }

        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y_end, s_bounce);
        xSemaphoreTake(s_flush_done, portMAX_DELAY);
    }
    lv_display_flush_ready(disp);
}

/* LVGL tick */
void lv_tick_cb(void *arg) { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

/* ---------------- LVGL touch indev ---------------- */
void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_point_data_t point = {0};
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(s_tp);
    esp_lcd_touch_get_data(s_tp, &point, &cnt, 1);
    if (cnt > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ---------------- LVGL task ---------------- */
void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    uint32_t last_refresh = 0;
    while (1) {
        xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
        uint32_t wait_ms = lv_timer_handler();

        /* The AXS15231B blanks itself after ~700 ms of idle. Force a
         * full-screen invalidate every 200 ms so the flush keeps firing. */
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_refresh >= 200) {
            lv_obj_invalidate(lv_screen_active());
            last_refresh = now;
        }

        xSemaphoreGive(s_lvgl_mux);
        if (wait_ms > 50) wait_ms = 50;
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

/* ---------------- Backlight ---------------- */
void backlight_init(void)
{
    gpio_config_t bk = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
    };
    ESP_ERROR_CHECK(gpio_config(&bk));
    gpio_set_level(PIN_LCD_BL, 1);
}

/* ---------------- Display bring-up ---------------- */
void init_display(void)
{
    ESP_LOGI(TAG, "Init QSPI bus");
    const spi_bus_config_t bus_cfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK,
        PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
        LCD_H_RES * BOUNCE_LINES * sizeof(uint16_t) + 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Init panel IO");
    esp_lcd_panel_io_spi_config_t io_cfg =
        AXS15231B_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io));

    ESP_LOGI(TAG, "Init AXS15231B panel with vendor init sequence");
    const axs15231b_vendor_config_t vendor_cfg = {
        .init_cmds      = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .data_endian    = LCD_RGB_DATA_ENDIAN_BIG,
        .vendor_config  = (void *)&vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(s_io, &panel_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    /* NOTE: do NOT call esp_lcd_panel_disp_on_off(s_panel, true) here.
     * The vendor init sequence (0x11 sleep-out + 0x29 display-on) has
     * already powered the display. Re-issuing 0x29 via the driver can
     * put the panel into a bad state on this chip. */

    s_flush_done = xSemaphoreCreateBinary();
    const esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = on_color_trans_done };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL));
}

/* ---------------- Touch bring-up ---------------- */
void init_touch(void)
{
    ESP_LOGI(TAG, "Init I2C master for touch");
    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = TOUCH_I2C_PORT,
        .sda_io_num = PIN_TOUCH_SDA,
        .scl_io_num = PIN_TOUCH_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &bus));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    tp_io_cfg.scl_speed_hz = TOUCH_I2C_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags  = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs15231b(tp_io, &tp_cfg, &s_tp));
}

/*
 * Layout (3 columns x 3 rows, each cell ~107x160 px on a 320x480 screen):
 *
 *   [ Red      ] [ Green    ] [ Blue     ]   <- RGB
 *   [ Cyan     ] [ Magenta  ] [ Yellow   ]   <- CMY
 *   [ Key/Blk  ] [ Black    ] [ White    ]   <- K + Black + White
 */
void ui_initialisation_create(lv_display_t *disp)
{
    s_grid_disp = disp;
    static const uint32_t color_hex[9] = {
        0xFF0000, 0x00FF00, 0x0000FF,
        0x00FFFF, 0xFF00FF, 0xFFFF00,
        0x000000, 0x000000, 0xFFFFFF,
    };

    static const char *const names[9] = {
        "Red",     "Green",   "Blue",
        "Cyan",    "Magenta", "Yellow",
        "Key",     "Black",   "White",
    };

    static int32_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    static int32_t row_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_set_style_pad_column(scr, 0, 0);
    lv_obj_set_layout(scr, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(scr, col_dsc, row_dsc);

    for (int i = 0; i < 9; i++) {
        int col = i % 3;
        int row = i / 3;

        lv_obj_t *rect = lv_obj_create(scr);
        lv_obj_set_style_bg_color(rect, lv_color_hex(color_hex[i]), 0);
        lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rect, 0, 0);
        lv_obj_set_style_pad_all(rect, 0, 0);
        lv_obj_set_style_radius(rect, 0, 0);
        lv_obj_set_grid_cell(rect,
                             LV_GRID_ALIGN_STRETCH, col, 1,
                             LV_GRID_ALIGN_STRETCH, row, 1);

        lv_obj_t *label = lv_label_create(rect);
        lv_label_set_text_fmt(label, "%s\n[%d,%d]", names[i], row, col);
        lv_obj_set_style_text_color(label, lv_color_hex(0x808080), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
    }

    lv_timer_t *t = lv_timer_create(on_grid_timer, 5000, NULL);
    lv_timer_set_repeat_count(t, 1);
}
