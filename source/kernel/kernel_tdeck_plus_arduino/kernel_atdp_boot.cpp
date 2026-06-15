// kernel_atdp_boot.cpp — Arduino-backed boot for T-Deck Plus
//
// Uses Arduino Wire for I2C instead of IDF 5.3 i2c_master (which has a
// known NACK/ESP_ERR_INVALID_STATE regression on this hardware).
//
// Baked-in (Layer 0):
//   ST7789  — SPI display  (CS=12 DC=11 MOSI=41 SCLK=40 RST=-1 BL=42)
//   GT911   — I2C touch    (SDA=18 SCL=8 INT=16 RST=NC) via Wire
//   Trackball — GPIO       (UP=3 DN=15 LT=1 RT=2 CLK=0)
//   BBQ20   — I2C keyboard (SDA=18 SCL=8 addr=0x55) via Wire

#include "Arduino.h"
#include "Wire.h"
#include "driver/gpio.h"
#include <sys/stat.h>
#include <string.h>

extern "C" {
#include "../kernel_arduino/kernel_arduino.h"
#include "../../drivers/display/st7789/st7789.h"
#include "../../drivers/input/trackball/trackball.h"
}

// ── Pin assignments ───────────────────────────────────────────────────────────

#define TDP_DISPLAY_CS    12
#define TDP_DISPLAY_DC    11
#define TDP_DISPLAY_MOSI  41
#define TDP_DISPLAY_SCLK  40
#define TDP_DISPLAY_RST   (-1)
#define TDP_DISPLAY_BL    42

#define TDP_I2C_SDA       18
#define TDP_I2C_SCL        8
#define TDP_TOUCH_INT     16
#define TDP_BOARD_POWERON 10

#define GT911_ADDR        0x5D
#define GT911_ADDR_ALT    0x14
#define GT911_REG_STATUS  0x814E
#define GT911_REG_POINT1  0x8150
#define GT911_REG_PRODUCT 0x8140

#define BBQ20_ADDR        0x55

static const char *TAG = "atdp_boot";

// ── GT911 Wire helpers ────────────────────────────────────────────────────────

static uint8_t s_gt911_addr = GT911_ADDR;
static bool    s_gt911_ok   = false;

static bool gt911_write_reg(uint16_t reg, uint8_t val)
{
    Wire.beginTransmission(s_gt911_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool gt911_read_reg(uint16_t reg, uint8_t *buf, size_t len)
{
    Wire.beginTransmission(s_gt911_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(s_gt911_addr, (uint8_t)len);
    for (size_t i = 0; i < len; i++) {
        if (!Wire.available()) return false;
        buf[i] = Wire.read();
    }
    return true;
}

static bool gt911_probe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)(GT911_REG_PRODUCT >> 8));
    Wire.write((uint8_t)(GT911_REG_PRODUCT & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(addr, (uint8_t)4);
    char pid[5] = {0};
    for (int i = 0; i < 4 && Wire.available(); i++) pid[i] = (char)Wire.read();
    ESP_LOGI(TAG, "GT911 at 0x%02X — ID: %s", addr, pid);
    return true;
}

// ── catcall_touch_t implementation ────────────────────────────────────────────

static bool gt911_catcall_is_pressed(void)
{
    if (!s_gt911_ok) return false;
    uint8_t status = 0;
    if (!gt911_read_reg(GT911_REG_STATUS, &status, 1)) return false;
    return (status & 0x80) && (status & 0x0F) > 0;
}

static bool gt911_catcall_read_point(uint16_t *x, uint16_t *y)
{
    if (!s_gt911_ok || !x || !y) return false;
    uint8_t status = 0;
    if (!gt911_read_reg(GT911_REG_STATUS, &status, 1)) return false;
    if (!(status & 0x80) || (status & 0x0F) == 0) return false;

    uint8_t pt[5] = {0};
    bool ok = gt911_read_reg(GT911_REG_POINT1, pt, sizeof(pt));
    gt911_write_reg(GT911_REG_STATUS, 0x00);
    if (!ok) return false;

    // pt: [track_id, x_low, x_high, y_low, y_high]
    *x = (uint16_t)pt[1] | ((uint16_t)pt[2] << 8);
    *y = (uint16_t)pt[3] | ((uint16_t)pt[4] << 8);
    return true;
}

static esp_err_t gt911_catcall_init(const touch_config_t *cfg)   { return ESP_OK; }
static esp_err_t gt911_catcall_deinit(void)                      { return ESP_OK; }

static const catcall_touch_t s_touch_catcall = {
    .name            = "gt911_wire",
    .catcall_version = CATCALL_TOUCH_VERSION,
    .init            = gt911_catcall_init,
    .read_point      = gt911_catcall_read_point,
    .is_pressed      = gt911_catcall_is_pressed,
    .deinit          = gt911_catcall_deinit,
};

// ── BBQ20 keyboard (Wire) ─────────────────────────────────────────────────────

static QueueHandle_t s_kb_queue = NULL;

static void bbq20_poll_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20));
        Wire.requestFrom((uint8_t)BBQ20_ADDR, (uint8_t)1);
        if (!Wire.available()) continue;
        uint8_t key = Wire.read();
        if (key == 0) continue;
        input_event_t ev = { .type = INPUT_EVENT_KEY_DOWN, .keycode = key };
        if (s_kb_queue) xQueueSend(s_kb_queue, &ev, 0);
    }
}

static bool bbq20_poll_event(input_event_t *out)
{
    if (!s_kb_queue || !out) return false;
    return xQueueReceive(s_kb_queue, out, 0) == pdTRUE;
}

static esp_err_t bbq20_catcall_init(void)  { return ESP_OK; }
static esp_err_t bbq20_catcall_deinit(void){ return ESP_OK; }

static const catcall_input_t s_kb_catcall = {
    .name            = "bbq20_wire",
    .catcall_version = CATCALL_INPUT_VERSION,
    .init            = bbq20_catcall_init,
    .poll_event      = bbq20_poll_event,
    .deinit          = bbq20_catcall_deinit,
};

// ── Serial console ────────────────────────────────────────────────────────────

static void serial_console_task(void *arg)
{
    // Use Arduino Serial — avoids uart_driver_install conflict with Arduino framework
    Serial.begin(115200);
    while (!Serial) vTaskDelay(pdMS_TO_TICKS(10));

    char line[32]; int len = 0;
    Serial.print("\r\nPURR OS console (atdp) — commands: scan\r\n> ");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\r' || c == '\n') {
                Serial.print("\r\n");
                line[len] = '\0'; len = 0;
                if (strcmp(line, "scan") == 0) {
                    Serial.print("[SCAN] Wire I2C scan (SDA=18 SCL=8):\r\n");
                    int found = 0;
                    for (uint8_t a = 0x03; a <= 0x77; a++) {
                        Wire.beginTransmission(a);
                        if (Wire.endTransmission() == 0) {
                            Serial.printf("[SCAN]   0x%02X ACK\r\n", a);
                            found++;
                        }
                    }
                    if (!found) Serial.print("[SCAN]   no devices found\r\n");
                    Serial.printf("[SCAN] done (%d device(s))\r\n", found);
                } else if (line[0]) {
                    Serial.printf("unknown: %s\r\n", line);
                }
                Serial.print("> ");
            } else if ((c == 0x7F || c == '\b') && len > 0) {
                len--; Serial.print("\b \b");
            } else if (len < (int)sizeof(line) - 1 && c >= 0x20) {
                line[len++] = c; Serial.print(c);
            }
        }
    }
}

// ── app_main ──────────────────────────────────────────────────────────────────

static void ensure_sd_dirs(void)
{
    if (!purr_kernel_sd_available()) return;
    const char *dirs[] = {
        "/sdcard/apps", "/sdcard/modules", "/sdcard/drivers",
        "/sdcard/drivers/display", "/sdcard/drivers/touch",
        "/sdcard/drivers/input", "/sdcard/drivers/radio",
        "/sdcard/drivers/gps", "/sdcard/system", "/sdcard/system/logs", NULL
    };
    for (int i = 0; dirs[i]; i++) {
        struct stat st;
        if (stat(dirs[i], &st) != 0) mkdir(dirs[i], 0755);
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "PURR OS %s / KITT %s  T-Deck Plus (Arduino kernel) booting...",
             PURR_KERNEL_VERSION, KITT_VERSION);

    arduino_kernel_nvs_init();
    arduino_kernel_spiffs_init(TAG);
    ensure_sd_dirs();

    ESP_LOGI(TAG, "=== phase 0: baked-in drivers ===");

    // BOARD_POWERON — gates all I2C/SPI peripherals
    // INT LOW before power-on anchors GT911 I2C address at 0x5D
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_16, 0);
    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_10, 1);
    ESP_LOGI(TAG, "BOARD_POWERON HIGH");
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Display — ST7789 SPI (hand-rolled driver, unchanged)
    st7789_configure(TDP_DISPLAY_CS, TDP_DISPLAY_DC, TDP_DISPLAY_MOSI,
                     TDP_DISPLAY_SCLK, TDP_DISPLAY_RST, TDP_DISPLAY_BL);
    if (st7789_drv_init() != 0) {
        purr_kernel_panic("ST7789 display init failed");
    }

    // I2C via Arduino Wire — bypasses IDF 5.3 i2c_master regression
    Wire.begin(TDP_I2C_SDA, TDP_I2C_SCL);
    Wire.setClock(400000);
    vTaskDelay(pdMS_TO_TICKS(300));

    // GT911 touch — probe both addresses
    if (gt911_probe(GT911_ADDR)) {
        s_gt911_addr = GT911_ADDR;
        s_gt911_ok   = true;
    } else if (gt911_probe(GT911_ADDR_ALT)) {
        s_gt911_addr = GT911_ADDR_ALT;
        s_gt911_ok   = true;
    } else {
        ESP_LOGW(TAG, "GT911 not found — touch disabled");
    }
    if (s_gt911_ok) {
        gt911_write_reg(GT911_REG_STATUS, 0x00);
        purr_kernel_register_touch(&s_touch_catcall);
        ESP_LOGI(TAG, "GT911 touch ready at 0x%02X (Wire)", s_gt911_addr);
    }

    // Trackball — GPIO (unchanged)
    if (trackball_drv_init() != 0)
        ESP_LOGW(TAG, "trackball init failed");

    // BBQ20 keyboard — polled via Wire
    Wire.beginTransmission(BBQ20_ADDR);
    if (Wire.endTransmission() == 0) {
        s_kb_queue = xQueueCreate(32, sizeof(input_event_t));
        xTaskCreate(bbq20_poll_task, "bbq20", 2048, NULL, 3, NULL);
        purr_kernel_register_input(&s_kb_catcall);
        ESP_LOGI(TAG, "BBQ20 keyboard ready (Wire)");
    } else {
        ESP_LOGW(TAG, "BBQ20 not found — keyboard disabled");
    }

    ESP_LOGI(TAG, "baked-in drivers ready");

    ESP_LOGI(TAG, "=== phase 1: static modules ===");
    extern void purr_register_static_modules(void);
    purr_register_static_modules();
    purr_kernel_load_static_modules();

    if (purr_kernel_sd_available()) {
        ESP_LOGI(TAG, "=== phase 2: SD extras ===");
        purr_kernel_scan_modules("/sdcard/modules", NULL);
        purr_kernel_scan_modules("/sdcard/drivers", NULL);
    }

    ESP_LOGI(TAG, "boot complete — %u bytes free", (unsigned)purr_kernel_free_ram());
    xTaskCreate(serial_console_task, "serial_con", 4096, NULL, 1, NULL);

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
