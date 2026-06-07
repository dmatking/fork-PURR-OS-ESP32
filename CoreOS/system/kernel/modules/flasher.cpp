// flasher.cpp — PURR OS firmware flasher (pure ESP-IDF)
// Reads a firmware image from SPIFFS and flashes via OTA API.

#include "flasher.h"
#include "../purr_idf_compat.h"
#include "esp_spiffs.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef PURR_DISPLAY_SSD1306
#  include "display_ssd1306.h"
#endif
#ifdef PURR_DISPLAY_ILI9341
#  include "display_ili9341.h"
#endif

static const char* TAG = "flasher";

static void flasher_text(device_config_t* cfg, uint8_t row, const char* text) {
#ifdef PURR_DISPLAY_SSD1306
    if (strcmp(cfg->display, "ssd1306") == 0) { display_ssd1306_text(row, text); return; }
#endif
#ifdef PURR_DISPLAY_ILI9341
    if (strcmp(cfg->display, "ili9341") == 0) { display_ili9341_text(row, text); return; }
#endif
    ESP_LOGI(TAG, "row%d: %s", row, text);
}

static void flasher_clear(device_config_t* cfg) {
#ifdef PURR_DISPLAY_SSD1306
    if (strcmp(cfg->display, "ssd1306") == 0) { display_ssd1306_clear(); return; }
#endif
#ifdef PURR_DISPLAY_ILI9341
    if (strcmp(cfg->display, "ili9341") == 0) { display_ili9341_clear(); return; }
#endif
}

static bool nvs_clear_boot_flag() {
    nvs_handle_t h;
    if (nvs_open("kitt_boot", NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_erase_key(h, "flash_flag");
    nvs_commit(h);
    nvs_close(h);
    return true;
}

static bool spiffs_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool ota_write_image(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    struct stat st;
    if (stat(path, &st) != 0) { fclose(f); return false; }
    size_t filesize = (size_t)st.st_size;

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) { fclose(f); return false; }

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
        fclose(f); return false;
    }

    uint8_t buf[512];
    size_t written = 0;
    while (written < filesize) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        if (esp_ota_write(handle, buf, n) != ESP_OK) {
            fclose(f); esp_ota_abort(handle); return false;
        }
        written += n;
    }
    fclose(f);

    if (esp_ota_end(handle) != ESP_OK) return false;
    return esp_ota_set_boot_partition(update_partition) == ESP_OK;
}

void flasher_init()   {}
void flasher_update() {}
void flasher_deinit() {}

void flasher_run(device_config_t* cfg) {
#ifdef PURR_DISPLAY_SSD1306
    if (strcmp(cfg->display, "ssd1306") == 0) display_ssd1306_init();
#endif
#ifdef PURR_DISPLAY_ILI9341
    if (strcmp(cfg->display, "ili9341") == 0) display_ili9341_init();
#endif

    flasher_clear(cfg);
    flasher_text(cfg, 0, "PURR OS Flasher");
    flasher_text(cfg, 1, "Looking...");

    bool has_bin  = spiffs_file_exists("/spiffs/update/update_firmware.bin");
    bool has_purr = spiffs_file_exists("/spiffs/update/update_firmware.purr");

    if (!has_bin && !has_purr) {
        flasher_text(cfg, 2, "ERR: No image");
        flasher_text(cfg, 3, "/update/*.bin");
        flasher_text(cfg, 4, "PWR to cancel");
        vTaskDelay(pdMS_TO_TICKS(10000));
        nvs_clear_boot_flag();
        esp_restart();
        return;
    }

    const char* img = has_bin
        ? "/spiffs/update/update_firmware.bin"
        : "/spiffs/update/update_firmware.purr";
    flasher_text(cfg, 2, "Writing...");

    bool ok = ota_write_image(img);
    if (ok) {
        flasher_text(cfg, 3, "Done. Rebooting");
        nvs_clear_boot_flag();
        remove(img);
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    } else {
        flasher_text(cfg, 3, "ERR: Flash fail");
        flasher_text(cfg, 4, "Image kept.");
        nvs_clear_boot_flag();
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
}
