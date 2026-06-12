# Partition Manager

The partition manager (`pm_*`) handles SD card mounting, OTA slot management, and firmware install/backup. It's compiled when `PURR_HAS_PARTITION_MGR` is defined (CYD targets).

---

## OTA partition layout (CYD)

```
factory  0x010000  1.06 MB   PURR bootloader (chainloads ota_0 or ota_1)
ota_0    0x120000  1.44 MB   Primary OS slot
ota_1    0x290000  1.00 MB   OTA update slot
spiffs   0x390000  448 KB    Read-only assets
```

The factory bootloader checks NVS for a pending OTA slot and boots accordingly. `pm_boot_to_factory()` resets the boot target back to factory.

---

## pm_* API

```cpp
#include "partition_manager.h"

// Init — mounts SD card (SPI3_HOST: CS=5, MOSI=23, MISO=19, CLK=18)
// and scans OTA partitions. Called from kitt.init() step 10.5.
void pm_init();

// SD card
bool  pm_sd_available();                    // true if mounted
int   pm_sd_list(pm_sd_file_t *out, int max, const char *dir);
//   pm_sd_file_t: name[64], path[256], size_bytes

// OTA partition info
int   pm_slot_count();                      // number of OTA slots (usually 2)
bool  pm_slot_info(int slot, pm_slot_t *out);
//   pm_slot_t: slot(int), name[16], valid(bool), size_bytes, label[16]

// Flash .bin from SD to an OTA slot
// progress_cb(bytes_written, total_bytes) — can be NULL
bool  pm_install(const char *sd_path, int slot,
                 void (*progress_cb)(size_t, size_t));

// Erase OTA slot
bool  pm_delete(int slot);

// Set next boot target and restart
bool  pm_launch(int slot);

// Backup OTA slot to SD
bool  pm_dump_to_sd(int slot, const char *sd_path);

// OTA boot management
int   pm_boot_slot();           // currently booted slot index
void  pm_boot_to_factory();     // set next boot = factory, then esp_restart()
```

---

## Installing firmware from SD

```cpp
// Copy a .bin file from SD to ota_1, then reboot into it
if (pm_sd_available() && pm_slot_count() >= 2) {
    bool ok = pm_install("/sdcard/purr_update.bin", 1,
        [](size_t written, size_t total) {
            // show progress bar: written / total
        });
    if (ok) {
        pm_launch(1);   // reboot into ota_1
    }
}
```

---

## Rollback

```cpp
// If update is bad, boot back to ota_0 from the factory loader:
pm_boot_to_factory();   // sets NVS boot=factory, restarts
// Factory loader will chainload ota_0 (the last known-good slot)
```

---

## Debug shell commands

When `PURR_HAS_SHELL` is defined, the USB serial REPL exposes:

```
pm-slots            list OTA slots and their validity
pm-boot <n>         set next boot slot (0 or 1) and restart
pm-install <file>   flash /sdcard/<file> to free OTA slot
pm-dump <slot>      dump OTA slot to /sdcard/slot<n>_backup.bin
pm-factory          reboot to factory bootloader
```
