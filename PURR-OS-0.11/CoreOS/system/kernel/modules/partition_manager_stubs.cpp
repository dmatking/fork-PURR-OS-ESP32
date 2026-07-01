// partition_manager_stubs.cpp — Minimal stubs for cyd_boot (no SD card support)
// Full implementation in partition_manager.cpp is only used in full OS build (cyd target)

#include "partition_manager.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG __attribute__((unused)) = "pm";

void pm_init() {
    // Minimal init: just NVS, no SD card
}

uint8_t pm_slot_count() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < 2; i++) {
        const esp_partition_t* part = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + i),
            NULL);
        if (part) count++;
    }
    return count;
}

bool pm_slot_info(uint8_t slot, pm_slot_t* out) {
    if (!out) return false;
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot),
        NULL);
    if (!part) return false;

    out->slot = slot;
    // Check magic byte for valid firmware
    uint32_t magic = 0;
    esp_partition_read(part, 0, &magic, sizeof(magic));
    out->valid = (magic & 0xFF) == 0xE9;
    out->size_bytes = out->valid ? part->size : 0;
    snprintf(out->label, sizeof(out->label), "ota_%u", slot);

    if (out->valid) {
        // Try to get name from NVS
        char key[12]; snprintf(key, sizeof(key), "s%u_name", slot);
        nvs_handle_t h;
        out->name[0] = '\0';
        if (nvs_open("purr_pm", NVS_READONLY, &h) == ESP_OK) {
            size_t l = PM_NAME_LEN;
            nvs_get_str(h, key, out->name, &l);
            nvs_close(h);
        }
        if (out->name[0] == '\0')
            snprintf(out->name, PM_NAME_LEN, "Firmware %u", slot);
    } else {
        out->name[0] = '\0';
    }
    return true;
}

bool pm_launch(uint8_t slot) {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot),
        NULL);
    if (!part) return false;

    uint32_t magic = 0;
    esp_partition_read(part, 0, &magic, sizeof(magic));
    if ((magic & 0xFF) != 0xE9) return false;

    if (esp_ota_set_boot_partition(part) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return true;
}

bool pm_delete(uint8_t slot) {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot),
        NULL);
    if (!part) return false;

    if (esp_partition_erase_range(part, 0, part->size) != ESP_OK) return false;

    // Clear NVS name
    char key[12]; snprintf(key, sizeof(key), "s%u_name", slot);
    nvs_handle_t h;
    if (nvs_open("purr_pm", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, key);
        nvs_commit(h);
        nvs_close(h);
    }
    return true;
}

bool pm_sd_available() { return false; }

int pm_sd_list(pm_sd_file_t* files, int max) {
    (void)files; (void)max;
    return 0;
}

int pm_boot_slot() {
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    if (!boot) return -1;

    for (uint8_t i = 0; i < 2; i++) {
        const esp_partition_t* p = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + i),
            NULL);
        if (p && p->address == boot->address) return (int)i;
    }
    return -1;
}

bool pm_dump_to_sd(uint8_t slot, const char* sd_path, pm_progress_cb_t cb) {
    (void)slot; (void)sd_path; (void)cb;
    return false;
}

bool pm_boot_to_factory() {
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_FACTORY,
        NULL);
    if (!factory) return false;
    if (esp_ota_set_boot_partition(factory) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return true;
}

bool pm_install(uint8_t slot, const char* sd_path, const char* display_name, pm_progress_cb_t cb) {
    (void)slot; (void)sd_path; (void)display_name; (void)cb;
    return false;
}
