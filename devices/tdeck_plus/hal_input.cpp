// T-Deck Plus input HAL — keyboard (I2C 0x55) + trackball (GPIO) + MiniWin mouse cursor
//
// Keyboard:  ESP32-C3 co-processor, I2C addr 0x55, SDA=18, SCL=8 (shared with GT911)
//            INT pin GPIO 46 — polled at hal_timer tick, interrupt used as hint only
// Trackball: UP=3  DOWN=15  LEFT=1  RIGHT=2  CLICK=0  (active LOW, internal pull-up)
//            Each pulse moves cursor by TRACKBALL_STEP pixels; held pulses accelerate
// Cursor:    Drawn by mw_paint_all() via hal_input_draw_cursor(); hides after 5 seconds
//            of inactivity so touch can take over cleanly

#include "hal_input.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "tdeck_input";

// ── Pin definitions ───────────────────────────────────────────────────────────
#define KB_SDA          18
#define KB_SCL           8
#define KB_ADDR       0x55
#define KB_INT          46

#define TB_UP            3
#define TB_DOWN         15
#define TB_LEFT          1
#define TB_RIGHT         2
#define TB_CLICK         0

// ── Cursor config ─────────────────────────────────────────────────────────────
#define CURSOR_HIDE_TICKS   (5 * MW_TICKS_PER_SECOND)   // 5 seconds at MW tick rate
#define TRACKBALL_STEP       4                            // pixels per pulse (base)
#define CURSOR_W             8
#define CURSOR_H            12

// ── State ─────────────────────────────────────────────────────────────────────
static i2c_master_bus_handle_t s_i2c_bus  = NULL;

// Provided by hal_touch.cpp — GT911 and keyboard share I2C_NUM_0 (SDA=18, SCL=8)
extern i2c_master_bus_handle_t tdeck_i2c_bus_handle(void);
static i2c_master_dev_handle_t s_kb_dev   = NULL;

static int16_t  s_cx = 160, s_cy = 120;   // cursor position
static uint32_t s_idle_ticks = 0;          // ticks since last trackball/key activity
static bool     s_cursor_visible = false;
static bool     s_click_pending  = false;

// Key ring buffer (32 chars)
#define KB_BUF_SIZE 32
static uint8_t  s_kb_buf[KB_BUF_SIZE];
static uint8_t  s_kb_head = 0, s_kb_tail = 0;

static inline void _kb_push(uint8_t c) {
    uint8_t next = (s_kb_tail + 1) % KB_BUF_SIZE;
    if (next != s_kb_head) { s_kb_buf[s_kb_tail] = c; s_kb_tail = next; }
}

static void _activity() {
    s_idle_ticks = 0;
    s_cursor_visible = true;
}

// ── I2C keyboard read ─────────────────────────────────────────────────────────
static void _kb_poll() {
    if (!s_kb_dev) return;
    uint8_t c = 0;
    // keyboard co-processor returns 0x00 when no key, otherwise the ASCII char
    if (i2c_master_receive(s_kb_dev, &c, 1, 20) == ESP_OK && c != 0x00) {
        _kb_push(c);
        _activity();
        ESP_LOGD(TAG, "key 0x%02x '%c'", c, (c >= 0x20 && c < 0x7F) ? c : '?');
    }
}

// ── Trackball GPIO read ───────────────────────────────────────────────────────
// Pulses are active-LOW. We edge-detect by comparing to previous state.
static uint8_t s_tb_prev = 0xFF;

static void _tb_poll() {
    uint8_t cur = 0;
    if (!gpio_get_level((gpio_num_t)TB_UP))    cur |= 0x01;
    if (!gpio_get_level((gpio_num_t)TB_DOWN))  cur |= 0x02;
    if (!gpio_get_level((gpio_num_t)TB_LEFT))  cur |= 0x04;
    if (!gpio_get_level((gpio_num_t)TB_RIGHT)) cur |= 0x08;
    if (!gpio_get_level((gpio_num_t)TB_CLICK)) cur |= 0x10;

    uint8_t pressed = cur & ~s_tb_prev;   // newly pressed this tick
    s_tb_prev = cur;

    if (!pressed) return;
    _activity();

    int16_t w = mw_hal_lcd_get_display_width();
    int16_t h = mw_hal_lcd_get_display_height();

    if (pressed & 0x01) s_cy -= TRACKBALL_STEP;
    if (pressed & 0x02) s_cy += TRACKBALL_STEP;
    if (pressed & 0x04) s_cx -= TRACKBALL_STEP;
    if (pressed & 0x08) s_cx += TRACKBALL_STEP;

    if (s_cx < 0) s_cx = 0;
    if (s_cy < 0) s_cy = 0;
    if (s_cx >= w) s_cx = (int16_t)(w - 1);
    if (s_cy >= h) s_cy = (int16_t)(h - 1);

    if (pressed & 0x10) s_click_pending = true;
}

// ── Public API ────────────────────────────────────────────────────────────────

void hal_input_init() {
    // GT911 (touch) already created the I2C_NUM_0 master bus in hal_touch.cpp.
    // Reuse that handle so keyboard (0x55) and touch (0x5D) share the same bus.
    s_i2c_bus = tdeck_i2c_bus_handle();
    if (!s_i2c_bus) {
        // Touch init hasn't run yet — create the bus here as fallback.
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port          = I2C_NUM_0;
        bus_cfg.sda_io_num        = (gpio_num_t)KB_SDA;
        bus_cfg.scl_io_num        = (gpio_num_t)KB_SCL;
        bus_cfg.clk_source        = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = true;
        if (i2c_new_master_bus(&bus_cfg, &s_i2c_bus) != ESP_OK) {
            ESP_LOGE(TAG, "keyboard I2C bus init failed");
            s_i2c_bus = NULL;
        }
    }

    if (s_i2c_bus) {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.device_address = KB_ADDR;
        dev_cfg.scl_speed_hz   = 100000;
        if (i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_kb_dev) != ESP_OK) {
            ESP_LOGE(TAG, "keyboard device add failed");
            s_kb_dev = NULL;
        } else {
            ESP_LOGI(TAG, "keyboard OK addr=0x%02X", KB_ADDR);
        }
    }

    // KB INT pin — input, pull-up (used as activity hint, main polling is timer-driven)
    gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << KB_INT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_cfg);

    // Trackball pins — input, pull-up, active LOW
    gpio_config_t tb_cfg = {
        .pin_bit_mask = (1ULL << TB_UP) | (1ULL << TB_DOWN) |
                        (1ULL << TB_LEFT) | (1ULL << TB_RIGHT) | (1ULL << TB_CLICK),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&tb_cfg);

    ESP_LOGI(TAG, "trackball OK UP=%d DN=%d L=%d R=%d CLK=%d",
             TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT, TB_CLICK);
}

// Called every MiniWin timer tick from hal_timer.cpp
void hal_input_tick() {
    _kb_poll();
    _tb_poll();

    if (s_cursor_visible) {
        s_idle_ticks++;
        if (s_idle_ticks >= CURSOR_HIDE_TICKS) {
            s_cursor_visible = false;
            mw_paint_all();   // repaint to erase cursor
        }
    }
}

// Called by purr_app.cpp paint to draw cursor on top of everything
void hal_input_draw_cursor(const mw_gl_draw_info_t *d) {
    if (!s_cursor_visible) return;

    int16_t x = s_cx, y = s_cy;

    // Draw solid mouse cursor with inverted background for visibility
    // Black outline
    mw_gl_set_fg_colour(MW_HAL_LCD_BLACK);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_ON);
    mw_gl_rectangle(d, x, y, (int16_t)(x + CURSOR_W), (int16_t)(y + CURSOR_H));

    // White fill with thin crosshair
    mw_gl_set_fg_colour(MW_HAL_LCD_WHITE);
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_rectangle(d, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(x + CURSOR_W - 1), (int16_t)(y + CURSOR_H - 1));

    // Crosshair in black
    mw_gl_set_fg_colour(MW_HAL_LCD_BLACK);
    mw_gl_set_fill(MW_GL_NO_FILL);
    mw_gl_vline(d, (int16_t)(x + CURSOR_W / 2), y, (int16_t)(y + CURSOR_H));
    mw_gl_hline(d, x, (int16_t)(x + CURSOR_W), (int16_t)(y + CURSOR_H / 2));
}

bool hal_input_cursor_visible()  { return s_cursor_visible; }
void hal_input_get_cursor(int16_t *x, int16_t *y) { *x = s_cx; *y = s_cy; }

bool hal_input_key_available() { return s_kb_head != s_kb_tail; }
uint8_t hal_input_key_read() {
    if (s_kb_head == s_kb_tail) return 0;
    uint8_t c = s_kb_buf[s_kb_head];
    s_kb_head = (s_kb_head + 1) % KB_BUF_SIZE;
    return c;
}

bool hal_input_click_pending() {
    if (!s_click_pending) return false;
    s_click_pending = false;
    return true;
}

void hal_input_notify_touch() {
    if (s_cursor_visible) {
        s_cursor_visible = false;
        mw_paint_all();
    }
    s_idle_ticks = 0;
}
