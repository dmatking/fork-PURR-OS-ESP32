#include "partition_manager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

static Preferences  prefs;
static SPIClass      sd_spi(VSPI);
static bool          sd_mounted = false;

// ── NVS helpers ───────────────────────────────────────────────────────────────

static void nvs_set_name(uint8_t slot, const char* name) {
    char key[12];
    snprintf(key, sizeof(key), "s%u_name", slot);
    prefs.begin("purr_pm", false);
    prefs.putString(key, name);
    prefs.end();
}

static void nvs_get_name(uint8_t slot, char* out, size_t len) {
    char key[12];
    snprintf(key, sizeof(key), "s%u_name", slot);
    prefs.begin("purr_pm", true);
    String s = prefs.getString(key, "");
    prefs.end();
    strncpy(out, s.c_str(), len - 1);
    out[len - 1] = '\0';
}

static void nvs_clear_name(uint8_t slot) {
    char key[12];
    snprintf(key, sizeof(key), "s%u_name", slot);
    prefs.begin("purr_pm", false);
    prefs.remove(key);
    prefs.end();
}

// ── Partition helpers ─────────────────────────────────────────────────────────

static esp_partition_subtype_t ota_subtype(uint8_t slot) {
    return (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot);
}

static const esp_partition_t* find_ota_slot(uint8_t slot) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ota_subtype(slot), NULL);
}

// Returns true if the OTA slot has a valid app image (not erased / factory default)
static bool slot_has_firmware(const esp_partition_t* part) {
    if (!part) return false;
    uint32_t magic = 0;
    esp_partition_read(part, 0, &magic, sizeof(magic));
    // ESP32 app images start with magic byte 0xE9
    return (magic & 0xFF) == 0xE9;
}

// ── Public API ────────────────────────────────────────────────────────────────

void pm_init() {
    sd_spi.begin(PM_SD_SCLK, PM_SD_MISO, PM_SD_MOSI, PM_SD_CS);
    if (SD.begin(PM_SD_CS, sd_spi, 20000000)) {
        sd_mounted = true;
        Serial.println("[pm] SD mounted");
    } else {
        Serial.println("[pm] SD not found");
    }
}

uint8_t pm_slot_count() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < PM_MAX_SLOTS; i++) {
        if (find_ota_slot(i)) count++;
    }
    return count;
}

bool pm_slot_info(uint8_t slot, pm_slot_t* out) {
    const esp_partition_t* part = find_ota_slot(slot);
    if (!part) return false;

    out->slot      = slot;
    out->valid     = slot_has_firmware(part);
    out->size_bytes = out->valid ? part->size : 0;
    snprintf(out->label, sizeof(out->label), "ota_%u", slot);

    if (out->valid) {
        nvs_get_name(slot, out->name, PM_NAME_LEN);
        if (out->name[0] == '\0') {
            snprintf(out->name, PM_NAME_LEN, "Firmware %u", slot);
        }
    } else {
        out->name[0] = '\0';
    }
    return true;
}

bool pm_launch(uint8_t slot) {
    const esp_partition_t* part = find_ota_slot(slot);
    if (!part || !slot_has_firmware(part)) {
        Serial.printf("[pm] launch slot %u: invalid\n", slot);
        return false;
    }

    esp_err_t err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        Serial.printf("[pm] set_boot_partition err %d\n", err);
        return false;
    }

    Serial.printf("[pm] launching slot %u — restarting\n", slot);
    delay(100);
    esp_restart();
    return true;  // unreachable
}

bool pm_delete(uint8_t slot) {
    const esp_partition_t* part = find_ota_slot(slot);
    if (!part) return false;

    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        Serial.printf("[pm] erase slot %u err %d\n", slot, err);
        return false;
    }
    nvs_clear_name(slot);
    Serial.printf("[pm] slot %u erased\n", slot);

    // If this was the active boot slot, reset boot target to factory so the
    // device doesn't try to boot the now-erased partition on next restart.
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    if (boot && boot->address == part->address) {
        const esp_partition_t* factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
        if (factory) esp_ota_set_boot_partition(factory);
    }
    return true;
}

bool pm_sd_available() {
    return sd_mounted;
}

int pm_sd_list(pm_sd_file_t* files, int max) {
    if (!sd_mounted) return 0;
    int count = 0;
    File root = SD.open("/");
    if (!root) return 0;

    while (count < max) {
        File f = root.openNextFile();
        if (!f) break;
        if (f.isDirectory()) { f.close(); continue; }

        const char* name = f.name();
        size_t len = strlen(name);
        bool is_bin  = len > 4 && strcmp(name + len - 4, ".bin")  == 0;
        bool is_purr = len > 5 && strcmp(name + len - 5, ".purr") == 0;
        if (is_bin || is_purr) {
            strncpy(files[count].name, name, PM_NAME_LEN - 1);
            files[count].name[PM_NAME_LEN - 1] = '\0';
            snprintf(files[count].path, PM_PATH_LEN, "/%s", name);
            files[count].size_bytes = f.size();
            count++;
        }
        f.close();
    }
    root.close();
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
    if (!part || !slot_has_firmware(part)) {
        Serial.printf("[pm] dump slot %u: invalid or empty\n", slot);
        return false;
    }
    if (!sd_mounted) {
        Serial.println("[pm] dump: SD not mounted");
        return false;
    }

    // Determine actual app size from image header to avoid dumping the full
    // erased partition. ESP32 image length is in the segment headers; fall back
    // to reading until the first 0xFF-filled 4KB page as a heuristic bound.
    // For simplicity, read up to partition size but stop writing once we hit the
    // end of real data (all 0xFF bytes beyond the app image are padding).
    // We use the simpler approach: read full partition, trim trailing 0xFF pages.
    size_t total = part->size;

    File f = SD.open(sd_path, FILE_WRITE);
    if (!f) {
        Serial.printf("[pm] dump: can't open %s for write\n", sd_path);
        return false;
    }

    if (cb) cb(0, "Reading...");

    uint8_t buf[512];
    size_t written = 0;
    size_t last_nonpad = 0;  // track furthest non-0xFF byte for trimming

    // First pass: stream to SD, track where real data ends
    for (size_t offset = 0; offset < total; offset += sizeof(buf)) {
        size_t n = ((total - offset) < sizeof(buf)) ? (total - offset) : sizeof(buf);
        if (esp_partition_read(part, offset, buf, n) != ESP_OK) {
            Serial.printf("[pm] dump: read error at 0x%x\n", offset);
            f.close();
            SD.remove(sd_path);
            return false;
        }
        f.write(buf, n);
        written += n;

        // Track last non-padding byte
        for (size_t i = n; i-- > 0; ) {
            if (buf[i] != 0xFF) { last_nonpad = offset + i + 1; break; }
        }

        if (cb) {
            int pct = (int)((uint64_t)written * 90 / total);
            cb(pct, "Reading...");
        }
    }
    f.close();

    // Truncate to actual app data (round up to 4-byte alignment)
    last_nonpad = (last_nonpad + 3) & ~3u;
    if (last_nonpad < total && last_nonpad > 0) {
        // Re-open and truncate — SD library doesn't support truncate directly,
        // so copy to a temp file then rename.
        File src = SD.open(sd_path, FILE_READ);
        String tmp = String(sd_path) + ".tmp";
        File dst = SD.open(tmp.c_str(), FILE_WRITE);
        if (src && dst) {
            size_t remaining = last_nonpad;
            while (remaining > 0) {
                size_t n = remaining < sizeof(buf) ? remaining : sizeof(buf);
                src.read(buf, n);
                dst.write(buf, n);
                remaining -= n;
            }
            src.close(); dst.close();
            SD.remove(sd_path);
            SD.rename(tmp.c_str(), sd_path);
        } else {
            if (src) src.close();
            if (dst) dst.close();
        }
    }

    if (cb) cb(100, "Done");
    Serial.printf("[pm] dump OK: slot %u → %s (%u bytes)\n", slot, sd_path, last_nonpad);
    return true;
}

bool pm_boot_to_factory() {
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory) {
        Serial.println("[pm] pm_boot_to_factory: no factory partition found");
        return false;
    }
    esp_err_t err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        Serial.printf("[pm] pm_boot_to_factory: set_boot err %d\n", err);
        return false;
    }
    Serial.println("[pm] rebooting to factory");
    delay(100);
    esp_restart();
    return true;
}

bool pm_install(uint8_t slot, const char* sd_path, const char* display_name,
                pm_progress_cb_t cb) {
    const esp_partition_t* part = find_ota_slot(slot);
    if (!part) {
        Serial.printf("[pm] install: no ota_%u partition\n", slot);
        return false;
    }

    if (!sd_mounted) {
        Serial.println("[pm] install: SD not mounted");
        return false;
    }

    File f = SD.open(sd_path, FILE_READ);
    if (!f) {
        Serial.printf("[pm] install: can't open %s\n", sd_path);
        return false;
    }

    size_t filesize = f.size();
    if (filesize == 0 || filesize > part->size) {
        Serial.printf("[pm] install: bad size %lu (part %lu)\n", (unsigned long)filesize, (unsigned long)part->size);
        f.close();
        return false;
    }

    // Validate ESP32 magic byte
    uint8_t magic = 0;
    f.read(&magic, 1);
    if (magic != 0xE9) {
        Serial.println("[pm] install: bad magic byte — not an ESP32 image");
        f.close();
        return false;
    }
    f.seek(0);

    if (cb) cb(0, "Erasing...");

    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(part, filesize, &handle);
    if (err != ESP_OK) {
        Serial.printf("[pm] ota_begin err %d\n", err);
        f.close();
        return false;
    }

    uint8_t buf[512];
    size_t written = 0;
    while (f.available()) {
        size_t n = f.read(buf, sizeof(buf));
        err = esp_ota_write(handle, buf, n);
        if (err != ESP_OK) {
            Serial.printf("[pm] ota_write err %d\n", err);
            esp_ota_abort(handle);
            f.close();
            return false;
        }
        written += n;
        if (cb) {
            int pct = (int)((uint64_t)written * 100 / filesize);
            cb(pct, "Writing...");
        }
    }
    f.close();

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        Serial.printf("[pm] ota_end err %d\n", err);
        return false;
    }

    nvs_set_name(slot, display_name && display_name[0] ? display_name : sd_path);
    if (cb) cb(100, "Done");
    Serial.printf("[pm] install OK: %s → slot %u\n", sd_path, slot);
    return true;
}
