// kernel_td_boot.c — specialized boot for T-Deck
//
// Identical to kernel_tdp_boot except no GPS module in phase 1.
//
// Baked-in (Layer 0):
//   ST7789  — SPI display  (CS=12 DC=11 MOSI=41 SCLK=40 RST=-1 BL=42)
//   Trackball — GPIO       (UP=3 DN=15 LT=1 RT=2 CLK=0)
//   BB Q20  — I2C keyboard (SDA=18 SCL=8 addr=0x55)
//   Note: T-Deck has no touch panel.
//
// Plug-and-play (Layer 4):
//   KittenUI, app_manager, SX1262 LoRa

#include "purr_kernel.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>
#include <string.h>

#include "../../drivers/display/st7789/st7789.h"
#include "../../drivers/input/trackball/trackball.h"
#include "../../drivers/input/bbq20/bbq20.h"

static const char *TAG = "td_boot";

#define TD_DISPLAY_CS    12
#define TD_DISPLAY_DC    11
#define TD_DISPLAY_MOSI  41
#define TD_DISPLAY_SCLK  40
#define TD_DISPLAY_RST   (-1)
#define TD_DISPLAY_BL    42

static void mount_flash_vfs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/flash",
        .partition_label        = NULL,
        .max_files              = 12,
        .format_if_mount_failed = false,
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
        "/sdcard/system", "/sdcard/system/logs", NULL
    };
    for (int i = 0; dirs[i]; i++) {
        struct stat st;
        if (stat(dirs[i], &st) != 0) mkdir(dirs[i], 0755);
    }
}

// ── Serial console ────────────────────────────────────────────────────────────

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

static void serial_console_task(void *arg)
{
    esp_err_t ret = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        vTaskDelete(NULL);
        return;
    }

    char line[32];
    int  len = 0;
    printf("\r\nPURR OS console (td) — commands: kb\r\n> ");
    fflush(stdout);

    for (;;) {
        uint8_t c = 0;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(50)) <= 0) continue;

        if (c == '\r' || c == '\n') {
            printf("\r\n");
            line[len] = '\0';
            len = 0;
            if (strcmp(line, "kb") == 0)       kb_test_loop();
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

void app_main(void)
{
    ESP_LOGI(TAG, "PURR OS %s / KITT %s  T-Deck booting...",
             PURR_KERNEL_VERSION, KITT_VERSION);

    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    mount_flash_vfs();
    ensure_sd_dirs();

    ESP_LOGI(TAG, "=== phase 0: baked-in drivers ===");

    st7789_configure(TD_DISPLAY_CS, TD_DISPLAY_DC, TD_DISPLAY_MOSI,
                     -1, TD_DISPLAY_SCLK, TD_DISPLAY_RST, TD_DISPLAY_BL);
    if (st7789_drv_init() != 0) {
        purr_kernel_panic("ST7789 display init failed");
    }

    if (trackball_drv_init() != 0) {
        ESP_LOGW(TAG, "trackball init failed — continuing");
    }

    if (bbq20_drv_init() != 0) {
        ESP_LOGW(TAG, "keyboard init failed — continuing");
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

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
