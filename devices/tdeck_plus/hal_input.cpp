// T-Deck Plus input HAL — keyboard (I2C 0x55) + trackball (GPIO, debug only)
//
// Keyboard:  ESP32-C3 co-processor, I2C 0x55, SDA=18, SCL=8, INT=46
//            Printable chars + Enter forwarded to MiniWin as MW_KEY_PRESSED_MESSAGE.
//
// Trackball: UP=3 DOWN=15 LEFT=1 RIGHT=2 CLICK=0 (active LOW, pull-up)
//            When PURR_DEBUG_INPUT is defined: prints every press/release to serial.
//            Does NOT generate nav events (removed).
//
// Enable debug: add -DPURR_DEBUG_INPUT=1 to your build, or use --debug-input flag.

#include "hal_input.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "hid";

// ── Keyboard pins ─────────────────────────────────────────────────────────────
#define KB_ADDR  0x55
#define KB_INT   46     // active LOW when key data available

// ── Trackball pins (active LOW) ───────────────────────────────────────────────
#define TB_UP     3
#define TB_DOWN  15
#define TB_LEFT   1
#define TB_RIGHT  2
#define TB_CLICK  0

// ── State ─────────────────────────────────────────────────────────────────────
extern i2c_master_bus_handle_t tdeck_i2c_bus_handle(void);
static i2c_master_dev_handle_t s_kb_dev = NULL;

#define KB_BUF_SIZE 32
static uint8_t s_kb_buf[KB_BUF_SIZE];
static uint8_t s_kb_head = 0, s_kb_tail = 0;

static inline void kb_push(uint8_t c) {
    uint8_t next = (s_kb_tail + 1) % KB_BUF_SIZE;
    if (next != s_kb_head) { s_kb_buf[s_kb_tail] = c; s_kb_tail = next; }
}

#define NAV_ENTER  0x0D

static mw_handle_t s_shell_handle = MW_INVALID_HANDLE;
void hal_input_set_shell_handle(mw_handle_t h) { s_shell_handle = h; }

static void fire_key(uint8_t code)
{
    mw_handle_t target = mw_find_window_with_focus();
    if (target == MW_INVALID_HANDLE) target = s_shell_handle;
    if (target == MW_INVALID_HANDLE) return;
    mw_post_message(MW_KEY_PRESSED_MESSAGE,
                    MW_INVALID_HANDLE, target,
                    (uint32_t)code, NULL, MW_WINDOW_MESSAGE);
}

// ── Init ──────────────────────────────────────────────────────────────────────
void hal_input_init(void)
{
    // INT pin
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << KB_INT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_cfg);

    // Keyboard I2C device
    i2c_master_bus_handle_t bus = tdeck_i2c_bus_handle();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus not ready — keyboard disabled");
    } else {
        i2c_device_config_t kb_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = KB_ADDR,
            .scl_speed_hz    = 100000,
        };
        esp_err_t err = i2c_master_bus_add_device(bus, &kb_cfg, &s_kb_dev);
        if (err == ESP_OK)
            ESP_LOGI(TAG, "keyboard OK addr=0x%02X INT=GPIO%d", KB_ADDR, KB_INT);
        else
            ESP_LOGE(TAG, "keyboard add_device: %s", esp_err_to_name(err));
    }

    // Trackball GPIO — always init so pins are not floating
    gpio_config_t tb_cfg = {
        .pin_bit_mask = (1ULL << TB_UP) | (1ULL << TB_DOWN) |
                        (1ULL << TB_LEFT) | (1ULL << TB_RIGHT) | (1ULL << TB_CLICK),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&tb_cfg);

#ifdef PURR_DEBUG_INPUT
    ESP_LOGI(TAG, "[DEBUG INPUT] trackball UP=%d DN=%d L=%d R=%d CLK=%d",
             TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT, TB_CLICK);
    ESP_LOGI(TAG, "[DEBUG INPUT] keyboard + trackball serial logging enabled");
#endif
}

// ── Tick ──────────────────────────────────────────────────────────────────────
void hal_input_tick(void)
{
    // ── Keyboard ──────────────────────────────────────────────────────────────
    if (s_kb_dev) {
        for (int n = 0; n < 8; n++) {
            int int_level = gpio_get_level((gpio_num_t)KB_INT);
            if (int_level != 0 && n > 0) break;

            uint8_t c = 0;
            esp_err_t err = i2c_master_receive(s_kb_dev, &c, 1,
                                               10 / portTICK_PERIOD_MS);
            if (err != ESP_OK || c == 0) break;

#ifdef PURR_DEBUG_INPUT
            if (c >= 0x20 && c < 0x7F)
                ESP_LOGI(TAG, "[KB] 0x%02X '%c'", c, c);
            else
                ESP_LOGI(TAG, "[KB] 0x%02X (ctrl)", c);
#endif

            kb_push(c);

            if (c == '\r' || c == '\n')      fire_key(NAV_ENTER);
            else if (c == '\b' || c == 0x7F) fire_key(c);
            else if (c >= 0x20 && c < 0x7F)  fire_key(c);
        }
    }

    // ── Trackball — debug serial only ─────────────────────────────────────────
#ifdef PURR_DEBUG_INPUT
    static uint8_t tb_prev = 0xFF;  // 0xFF = uninitialised

    uint8_t tb_now = 0;
    if (!gpio_get_level((gpio_num_t)TB_UP))    tb_now |= 0x01;
    if (!gpio_get_level((gpio_num_t)TB_DOWN))  tb_now |= 0x02;
    if (!gpio_get_level((gpio_num_t)TB_LEFT))  tb_now |= 0x04;
    if (!gpio_get_level((gpio_num_t)TB_RIGHT)) tb_now |= 0x08;
    if (!gpio_get_level((gpio_num_t)TB_CLICK)) tb_now |= 0x10;

    uint8_t pressed  = tb_now & ~tb_prev;
    uint8_t released = tb_prev & ~tb_now;

    if (pressed & 0x01) ESP_LOGI(TAG, "[TB] UP pressed");
    if (pressed & 0x02) ESP_LOGI(TAG, "[TB] DOWN pressed");
    if (pressed & 0x04) ESP_LOGI(TAG, "[TB] LEFT pressed");
    if (pressed & 0x08) ESP_LOGI(TAG, "[TB] RIGHT pressed");
    if (pressed & 0x10) ESP_LOGI(TAG, "[TB] CLICK pressed");

    if (released & 0x01) ESP_LOGI(TAG, "[TB] UP released");
    if (released & 0x02) ESP_LOGI(TAG, "[TB] DOWN released");
    if (released & 0x04) ESP_LOGI(TAG, "[TB] LEFT released");
    if (released & 0x08) ESP_LOGI(TAG, "[TB] RIGHT released");
    if (released & 0x10) ESP_LOGI(TAG, "[TB] CLICK released");

    tb_prev = tb_now;
#endif
}

// ── Key buffer ────────────────────────────────────────────────────────────────
bool hal_input_key_available(void) { return s_kb_head != s_kb_tail; }

uint8_t hal_input_key_read(void) {
    if (s_kb_head == s_kb_tail) return 0;
    uint8_t c = s_kb_buf[s_kb_head];
    s_kb_head = (s_kb_head + 1) % KB_BUF_SIZE;
    return c;
}

void hal_input_notify_touch(void) {}
