// partition_manager.cpp — OTA partition + SD card manager (pure ESP-IDF)

#include "partition_manager.h"
#include "../purr_idf_compat.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* TAG = "pm";
#define SD_MOUNT   "/sdcard"

static bool sd_mounted = false;
static sdmmc_card_t* s_card = NULL;

// ── NVS helpers ───────────────────────────────────────────────────────────────

static void nvs_set_name(uint8_t slot, const char* name) {
    char key[12]; snprintf(key, sizeof(key), "s%u_name", slot);
    nvs_handle_t h;
    if (nvs_open("purr_pm", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, key, name);
        nvs_commit(h); nvs_close(h);
    }
}

static void nvs_get_name(uint8_t slot, char* out, size_t len) {
    char key[12]; snprintf(key, sizeof(key), "s%u_name", slot);
    nvs_handle_t h;
    if (nvs_open("purr_pm", NVS_READONLY, &h) == ESP_OK) {
        size_t l = len;
        nvs_get_str(h, key, out, &l);
        nvs_close(h);
    }
}

static void nvs_clear_name(uint8_t slot) {
    char key[12]; snprintf(key, sizeof(key), "s%u_name", slot);
    nvs_handle_t h;
    if (nvs_open("purr_pm", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, key);
        nvs_commit(h); nvs_close(h);
    }
}

// ── Partition helpers ─────────────────────────────────────────────────────────

static const esp_partition_t* find_ota_slot(uint8_t slot) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot), NULL);
}

static bool slot_has_firmware(const esp_partition_t* part) {
    if (!part) return false;
    uint32_t magic = 0;
    esp_partition_read(part, 0, &magic, sizeof(magic));
    return (magic & 0xFF) == 0xE9;
}

// ── SD card init ──────────────────────────────────────────────────────────────

void pm_init() {
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.gpio_cs = (gpio_num_t)PM_SD_CS;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PM_SD_MOSI,
        .miso_io_num = PM_SD_MISO,
        .sclk_io_num = PM_SD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SPI_DMA_CH_AUTO);

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    if (esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &dev_cfg, &mount_cfg, &s_card) == ESP_OK) {
        sd_mounted = true;
        ESP_LOGI(TAG, "SD mounted: %lluMB", ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));
    } else {
        ESP_LOGW(TAG, "SD not found");
    }
}

uint8_t pm_slot_count() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < PM_MAX_SLOTS; i++)
        if (find_ota_slot(i)) count++;
    return count;
}

bool pm_slot_info(uint8_t slot, pm_slot_t* out) {
    const esp_partition_t* part = find_ota_slot(slot);
    if (!part) return false;
    out->slot       = slot;
    out->valid      = slot_has_firmware(part);
    out->size_bytes = out->valid ? part->size : 0;
    snprintf(out->label, sizeof(out->label), "ota_%u", slot);
    if (out->valid) {
        nvs_get_name(slot, out->name, PM_NAME_LEN);
        if (out->name[0] == '\0')
            snprintf(out->name, PM_NAME_LEN, "Firmware %u", slot);
    } else {
        out->name[0] = '\0';
    }
    return true;
}

bool pm_launch(uint8_t slot) {
    const esp_partition_t* part = find_ota_slot(slot);
    if (!part || !slot_has_firmware(part)) { ESP_LOGE(TAG, "launch slot %u: invalid", slot); return false; }
    if (esp_ota_set_boot_partition(part) != ESP_OK) return false;
    ESP_LOGI(TAG, "launching slot %u — restarting", slot);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return true;
}

bool pm_delete(uint8_t slot) {
    const esp_partition_t* part = find_ota_slot(slot);
    if (!part) return false;
    if (esp_partition_erase_range(part, 0, part->size) != ESP_OK) return false;
    nvs_clear_name(slot);
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    if (boot && boot->address == part->address) {
        const esp_partition_t* factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
        if (factory) esp_ota_set_boot_partition(factory);
    }
    return true;
}

bool pm_sd_available() { return sd_mounted; }

int pm_sd_list(pm_sd_file_t* files, int max) {
    if (!sd_mounted) return 0;
    int count = 0;
    DIR* dir = opendir(SD_MOUNT);
    if (!dir) return 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) && count < max) {
        if (entry->d_type == DT_DIR) continue;
        const char* name = entry->d_name;
        size_t len = strlen(name);
        bool is_bin  = len > 4 && strcmp(name + len - 4, ".bin")  == 0;
        bool is_purr = len > 5 && strcmp(name + len - 5, ".purr") == 0;
        if (is_bin || is_purr) {
            strncpy(files[count].name, name, PM_NAME_LEN - 1);
            files[count].name[PM_NAME_LEN - 1] = '\0';
            strncpy(files[count].path, name, PM_PATH_LEN - 1);
            files[count].path[PM_PATH_LEN - 1] = '\0';
            struct stat st; stat(files[count].path, &st);
            files[count].size_bytes = (size_t)st.st_size;
            count++;
        }
    }
    closedir(dir);
    return count;
}

int pm_boot_slot() {
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    if (!boot) return -1;
    for (uint8_t i = 0; i < PM_MAX_SLOTS; i++) {
        const esp_partition_t* p = find_ota_slot(i);
        if (p && p->address == boot->address) return (int)i;
    }
    return -1;
}

bool pm_dump_to_sd(uint8_t slot, const char* sd_path, pm_progress_cb_t cb) {
    const esp_partition_t* part = find_ota_slot(slot);
    if (!part || !slot_has_firmware(part)) return false;
    if (!sd_mounted) return false;

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT, sd_path);
    FILE* f = fopen(full_path, "wb");
    if (!f) { ESP_LOGE(TAG, "dump: can't open %s", full_path); return false; }

    if (cb) cb(0, "Reading...");
    uint8_t buf[512];
    size_t total = part->size, written = 0, last_nonpad = 0;

    for (size_t offset = 0; offset < total; offset += sizeof(buf)) {
        size_t n = ((total - offset) < sizeof(buf)) ? (total - offset) : sizeof(buf);
        if (esp_partition_read(part, offset, buf, n) != ESP_OK) {
            fclose(f); remove(full_path); return false;
        }
        fwrite(buf, 1, n, f);
        written += n;
        for (size_t i = n; i-- > 0;)
            if (buf[i] != 0xFF) { last_nonpad = offset + i + 1; break; }
        if (cb) cb((int)((uint64_t)written * 90 / total), "Reading...");
    }
    fclose(f);

    // Truncate to real app data
    last_nonpad = (last_nonpad + 3) & ~3u;
    if (last_nonpad < total && last_nonpad > 0) {
        char tmp[132]; snprintf(tmp, sizeof(tmp), "%s.tmp", full_path);
        FILE* src = fopen(full_path, "rb");
        FILE* dst = fopen(tmp, "wb");
        if (src && dst) {
            size_t rem = last_nonpad;
            while (rem > 0) {
                size_t n = (rem < sizeof(buf)) ? rem : sizeof(buf);
                fread(buf, 1, n, src); fwrite(buf, 1, n, dst); rem -= n;
            }
            fclose(src); fclose(dst);
            remove(full_path); rename(tmp, full_path);
        } else {
            if (src) fclose(src);
            if (dst) fclose(dst);
        }
    }

    if (cb) cb(100, "Done");
    ESP_LOGI(TAG, "dump OK: slot %u → %s (%u bytes)", slot, full_path, (unsigned)last_nonpad);
    return true;
}

bool pm_boot_to_factory() {
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory) return false;
    if (esp_ota_set_boot_partition(factory) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return true;
}

bool pm_install(uint8_t slot, const char* sd_path, const char* display_name,
                pm_progress_cb_t cb) {
    const esp_partition_t* part = find_ota_slot(slot);
    if (!part || !sd_mounted) return false;

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT, sd_path);
    FILE* f = fopen(full_path, "rb");
    if (!f) { ESP_LOGE(TAG, "install: can't open %s", full_path); return false; }

    struct stat st; stat(full_path, &st);
    size_t filesize = (size_t)st.st_size;

    uint8_t magic = 0;
    fread(&magic, 1, 1, f);
    if (magic != 0xE9) { fclose(f); ESP_LOGE(TAG, "bad magic byte"); return false; }
    fseek(f, 0, SEEK_SET);

    if (cb) cb(0, "Erasing...");
    esp_ota_handle_t handle;
    if (esp_ota_begin(part, filesize, &handle) != ESP_OK) { fclose(f); return false; }

    uint8_t buf[512];
    size_t written = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (esp_ota_write(handle, buf, n) != ESP_OK) {
            esp_ota_abort(handle); fclose(f); return false;
        }
        written += n;
        if (cb) cb((int)((uint64_t)written * 100 / filesize), "Writing...");
    }
    fclose(f);

    if (esp_ota_end(handle) != ESP_OK) return false;
    nvs_set_name(slot, display_name && display_name[0] ? display_name : sd_path);
    if (cb) cb(100, "Done");
    ESP_LOGI(TAG, "install OK: %s → slot %u", sd_path, slot);
    return true;
}
