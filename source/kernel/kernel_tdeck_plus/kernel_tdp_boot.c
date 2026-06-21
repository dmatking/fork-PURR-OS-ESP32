// kernel_tdp_boot.c — specialized boot for T-Deck Plus
//
// Inits display, touch, trackball, and keyboard directly before the module
// loader runs. This avoids module priority races and ensures all Layer 0
// catcalls are registered before KittenUI tries to use them.
//
// Baked-in (Layer 0, initialized here, NOT via module loader):
//   ST7789  — SPI display  (CS=12 DC=11 MOSI=41 SCLK=40 RST=-1 BL=42)
//   GT911   — I2C touch    (SDA=18 SCL=8 INT=16 RST=NC)
//   Trackball — GPIO       (UP=3 DN=15 LT=1 RT=2 CLK=0)
//   BB Q20  — I2C keyboard (SDA=18 SCL=8 addr=0x55)
//
// Plug-and-play (Layer 4, loaded by module loader):
//   MiniWin, app_manager, SX1276 LoRa, generic_nmea GPS

#include "purr_kernel.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>
#include <string.h>

// Baked-in driver headers
#include "../../drivers/display/st7789/st7789.h"
#include "../../drivers/touch/gt911/gt911.h"
#include "../../drivers/input/trackball/trackball.h"
#include "../../drivers/input/bbq20/bbq20.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"

static const char *TAG = "tdp_boot";

// ── T-Deck Plus pin assignments ───────────────────────────────────────────────

#define TDP_DISPLAY_CS    12
#define TDP_DISPLAY_DC    11
#define TDP_DISPLAY_MOSI  41
#define TDP_DISPLAY_SCLK  40
#define TDP_DISPLAY_RST   (-1)
#define TDP_DISPLAY_BL    42

// GT911 and BB Q20 share I2C bus on port 0
// Trackball uses GPIO only

// ── Flash VFS (SPIFFS) ────────────────────────────────────────────────────────

static void mount_flash_vfs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/flash",
        .partition_label        = NULL,
        .max_files              = 12,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed (%s)", esp_err_to_name(ret));
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info(NULL, &total, &used);
        ESP_LOGI(TAG, "flash VFS: %u KB / %u KB",
                 (unsigned)(used / 1024), (unsigned)(total / 1024));
    }
}

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

// ── Serial console ────────────────────────────────────────────────────────────
// Always-on task on UART0. Type 'kb' to echo BBQ20 keypresses to serial.
// Lets you confirm keyboard hardware works independent of the UI.

static void kb_test_loop(void)
{
    const catcall_input_t *kbd = purr_kernel_input();
    if (!kbd) {
        printf("[KB TEST] no keyboard catcall — bbq20 not ready\r\n");
        return;
    }
    printf("[KB TEST] press keys on the device keyboard. type 'q' here to exit.\r\n");
    fflush(stdout);
    for (;;) {
        uint8_t c = 0;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(0)) > 0) {
            if (c == 'q' || c == 3) break;
        }
        input_event_t ev;
        while (kbd->poll_event(&ev)) {
            if (ev.type == INPUT_EVENT_KEY_DOWN && ev.keycode) {
                uint16_t k = ev.keycode;
                if (k >= 0x20 && k <= 0x7E)
                    printf("[KB] '%c' (0x%02X)\r\n", (char)k, k);
                else
                    printf("[KB] 0x%02X\r\n", k);
                fflush(stdout);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("[KB TEST] done.\r\n");
}

static void i2c_scan_cmd(void)
{
    i2c_master_bus_handle_t bus = NULL;
    bool created = false;
    esp_err_t r = i2c_master_get_bus_handle(I2C_NUM_0, &bus);
    if (r != ESP_OK) {
        i2c_master_bus_config_t cfg = {
            .i2c_port          = I2C_NUM_0,
            .sda_io_num        = 18,
            .scl_io_num        = 8,
            .clk_source        = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) {
            printf("[SCAN] failed to acquire I2C bus\r\n");
            return;
        }
        created = true;
    }
    printf("[SCAN] full I2C sweep SDA=18 SCL=8:\r\n");
    int found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        esp_err_t res = i2c_master_probe(bus, addr, pdMS_TO_TICKS(10));
        if (res == ESP_OK) {
            const char *name = "";
            if (addr == 0x5D) name = " (GT911 primary)";
            else if (addr == 0x14) name = " (GT911 alt)";
            else if (addr == 0x55) name = " (BBQ20 keyboard)";
            printf("[SCAN]   0x%02X ACK%s\r\n", addr, name);
            found++;
        }
    }
    if (found == 0) printf("[SCAN]   no devices found\r\n");
    printf("[SCAN] done (%d device(s))\r\n", found);
    if (created) i2c_del_master_bus(bus);
}

static void serial_console_task(void *arg)
{
    esp_err_t ret = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        vTaskDelete(NULL);
        return;
    }

    char line[32];
    int  len = 0;
    printf("\r\nPURR OS console (tdp) — commands: kb, scan\r\n> ");
    fflush(stdout);

    for (;;) {
        uint8_t c = 0;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(50)) <= 0) continue;

        if (c == '\r' || c == '\n') {
            printf("\r\n");
            line[len] = '\0';
            len = 0;
            if (strcmp(line, "kb") == 0)       kb_test_loop();
            else if (strcmp(line, "scan") == 0) i2c_scan_cmd();
            else if (line[0] != '\0')           printf("unknown: %s\r\n", line);
            printf("> ");
            fflush(stdout);
        } else if ((c == 0x7F || c == '\b') && len > 0) {
            len--;
            printf("\b \b");
            fflush(stdout);
        } else if (len < (int)sizeof(line) - 1 && c >= 0x20) {
            line[len++] = (char)c;
            putchar(c);
            fflush(stdout);
        }
    }
}

// ── app_main ──────────────────────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "PURR OS %s / KITT %s  T-Deck Plus booting...",
             PURR_KERNEL_VERSION, KITT_VERSION);

    // NVS
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    mount_flash_vfs();
    ensure_sd_dirs();

    // ── Phase 0: baked-in hardware init ──────────────────────────────────────
    //
    // These run before ANY module is loaded, guaranteeing that display and
    // touch catcalls exist when KittenUI's init() checks for them.

    ESP_LOGI(TAG, "=== phase 0: baked-in drivers ===");

    // GT911 has no RST pin on T-Deck Plus (NC per LilyGO utilities.h).
    // INT=16, BOARD_POWERON=10.
    // Drive INT LOW before BOARD_POWERON so GT911 latches address 0x5D on power-up,
    // then release INT to input after 50ms startup margin.
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_16, 0);

    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_10, 1);
    ESP_LOGI(TAG, "BOARD_POWERON HIGH");
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Display — ST7789 SPI
    // ST7789 init includes a ~120ms SLPOUT delay internally, which counts
    // toward the GT911's required 300ms startup time after BOARD_POWERON.
    st7789_configure(TDP_DISPLAY_CS, TDP_DISPLAY_DC, TDP_DISPLAY_MOSI,
                     -1, TDP_DISPLAY_SCLK, TDP_DISPLAY_RST, TDP_DISPLAY_BL);
    if (st7789_drv_init() != 0) {
        purr_kernel_panic("ST7789 display init failed");
    }

    // Touch — GT911 I2C (creates I2C bus on port 0)
    // GT911 needs ~300ms from BOARD_POWERON before I2C responds.
    // Display init (SWRESET 150ms + ~30ms GRAM clear) counts toward that budget.
    // Wait the remainder — 400ms gives comfortable margin.
    // Use polling mode (int_pin=-1): avoids GPIO ISR and is simpler to diagnose.
    vTaskDelay(pdMS_TO_TICKS(400));

    // RST=17, matching PURR OS 0.11's devices/tdeck_plus/hal_touch.cpp
    // exactly — this "plug and play" driver previously configured RST as
    // not-connected (-1), skipping the hardware reset pulse 0.11 always
    // did before talking to the chip. Found by direct comparison against
    // 0.11's code once the touch instability traced back to "this only
    // started after the driver became plug-and-play."
    gt911_configure(18, 8, -1, 17, 0);   // SDA=18 SCL=8 poll-mode RST=17 port=0
    if (gt911_drv_init() != 0) {
        ESP_LOGW(TAG, "GT911 touch init failed — continuing without touch");
    }

    // Trackball — GPIO
    if (trackball_drv_init() != 0) {
        ESP_LOGW(TAG, "trackball init failed — continuing without trackball");
    }


    // Keyboard — BB Q20 (joins GT911's I2C bus on port 0)
    if (bbq20_drv_init() != 0) {
        ESP_LOGW(TAG, "BB Q20 keyboard init failed — continuing without keyboard");
    }

    ESP_LOGI(TAG, "baked-in drivers ready");

    // ── Phase 1: plug-and-play modules ───────────────────────────────────────
    //
    // Only modules NOT baked in above: KittenUI, app_manager, SX1276, GPS.
    // purr_register_static_modules() is generated by purrstrap from device.pcat
    // and omits display/touch/trackball/keyboard for this device.

    ESP_LOGI(TAG, "=== phase 1: static modules ===");
    extern void purr_register_static_modules(void);
    purr_register_static_modules();
    purr_kernel_load_static_modules();

    // ── Phase 2: SD extras ───────────────────────────────────────────────────

    if (purr_kernel_sd_available()) {
        ESP_LOGI(TAG, "=== phase 2: SD extras ===");
        purr_kernel_scan_modules("/sdcard/modules", NULL);
        purr_kernel_scan_modules("/sdcard/drivers", NULL);
    }

    if (!purr_kernel_display()) {
        ESP_LOGW(TAG, "no display catcall — check ST7789 init");
    }
    if (!purr_kernel_get_module("app_manager")) {
        ESP_LOGW(TAG, "app_manager not loaded");
    }

    ESP_LOGI(TAG, "boot complete — %u bytes free", (unsigned)purr_kernel_free_ram());

    xTaskCreate(serial_console_task, "serial_con", 4096, NULL, 1, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
