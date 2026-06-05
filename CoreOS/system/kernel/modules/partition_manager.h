#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Maximum OTA slots scanned (ota_0 … ota_3)
#define PM_MAX_SLOTS    4
#define PM_NAME_LEN     48
#define PM_PATH_LEN     128
#define PM_SD_MAX_FILES 16

// SD card VSPI pins for CYD
#define PM_SD_CS   5
#define PM_SD_MOSI 23
#define PM_SD_MISO 19
#define PM_SD_SCLK 18

typedef struct {
    uint8_t  slot;
    char     name[PM_NAME_LEN];   // user-visible name stored in NVS
    bool     valid;               // has a flashed firmware
    size_t   size_bytes;          // approximate firmware size
    char     label[16];           // e.g. "ota_0"
} pm_slot_t;

typedef struct {
    char name[PM_NAME_LEN];
    char path[PM_PATH_LEN];
    size_t size_bytes;
} pm_sd_file_t;

typedef void (*pm_progress_cb_t)(int percent, const char* status);

// Init — mounts SD, scans OTA partitions, loads NVS metadata
void    pm_init();

// Partition info
uint8_t pm_slot_count();
bool    pm_slot_info(uint8_t slot, pm_slot_t* out);

// Launch firmware in an OTA slot — sets boot partition + esp_restart().
// Never returns on success. Returns false if slot invalid/empty.
bool    pm_launch(uint8_t slot);

// Erase an OTA slot and remove its NVS entry
bool    pm_delete(uint8_t slot);

// SD card
bool    pm_sd_available();
int     pm_sd_list(pm_sd_file_t* files, int max);

// Flash a .bin from SD to an OTA slot. Progress 0-100 reported via cb (nullable).
bool    pm_install(uint8_t slot, const char* sd_path, const char* display_name,
                   pm_progress_cb_t cb);

// Dump a live OTA slot to a .bin file on SD. Mirror image of pm_install().
// path must be a full SD path, e.g. "/PURR_BACKUP.bin".
bool    pm_dump_to_sd(uint8_t slot, const char* sd_path, pm_progress_cb_t cb);

// Returns the OTA slot index that otadata will boot next (0, 1, …), or -1 if factory/none.
int     pm_boot_slot();

// Switch boot target to the factory partition and restart. Never returns on success.
bool    pm_boot_to_factory();
