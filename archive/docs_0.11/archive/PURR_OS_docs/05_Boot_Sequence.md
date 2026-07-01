# PURR OS — Boot Sequence
**v0.9.0 / KITT v0.5.0**

---

## Overview

PURR OS has two distinct boot paths depending on which partition is active:

- **Factory image** (`cyd_boot`) — a full recovery environment. Mounts SD card, scans OTA slots, shows a management UI. Chainloads `ota_0` automatically if it contains valid PURR firmware.
- **OTA image** (`cyd_s024c` / `heltec` / `tdeck`) — the full OS with KITT, MiniWin, shells, MicroPython.

On CYD, the factory image is flashed **once** to `0x10000` and never touched by OTA. The full OS lives in `ota_0` at `0x110000`. The ESP-IDF `otadata` partition (at `0xe000`) records which slot to boot next.

---

## CYD First Boot (after full flash)

```
ESP-IDF second-stage bootloader
  → reads otadata → points to factory (initial state after full flash)
  → loads factory image (cyd_boot) from 0x10000

cyd_boot
  → kitt.init()
      display_ili9341_init() + backlight ON
      touch_cst816s_init()
      pm_init()  ← mounts SD card on SPI bus
  → system_task
      bridge_start()
      [see Factory Boot Decision below]
```

---

## Factory Boot Decision

Every time `cyd_boot` boots, it runs this decision tree before showing any UI:

```
1. Read GPIO0
   └─ held LOW?  →  force_ui = true

2. Read NVS "purr_bl/boot_tries"
   └─ boot_tries >= 3?  →  sos_mode = true

3. Read esp_app_desc_t from ota_0 flash (no boot required)
   └─ project_name == "purr_os_core"?  →  is_purr = true

4. Decision:
   if is_purr AND !force_ui AND !sos_mode:
       nvs_set boot_tries = boot_tries + 1   ← increment before launch
       pm_launch(0)                          ← never returns
           esp_ota_set_boot_partition(ota_0)
           esp_restart()

   else if sos_mode:
       purr_bootloader_start(sos=true, boot_tries)

   else:
       purr_bootloader_start(sos=false, 0)
```

When `ota_0` boots successfully, KITT clears `boot_tries` during `kitt.init()`. If `ota_0` crashes before KITT initialises, the counter is never cleared and increments on the next factory boot.

---

## Factory Recovery UI (`purr_bootloader_start`)

The recovery UI runs as a FreeRTOS task on core 1. It uses `display_ili9341` and `touch_cst816s` directly — no MiniWin, no LVGL.

```
pm_init() already ran → SD card mounted (if present)

Screens:
  PB_HOME            — slot cards for each OTA slot
  PB_CONFIRM_BOOT    — confirm before switching boot partition
  PB_CONFIRM_WIPE    — confirm before erasing a slot
  PB_CONFIRM_BACKUP  — offer to back up PURR before overwriting
  PB_BACKING_UP      — progress bar while dumping slot to SD
  PB_INSTALL_SELECT  — .bin/.purr file picker from SD root
  PB_INSTALLING      — progress bar while writing SD file to OTA slot
  PB_POST_INSTALL    — offer to restore PURR or boot new firmware
  PB_RESTORING       — progress bar while restoring PURR from SD backup
  PB_SOS             — crash-loop alert: wipe / boot-anyway / dismiss

Actions available:
  BOOT   → esp_ota_set_boot_partition(slot) + esp_restart()
  WIPE   → esp_partition_erase_range() + clear NVS name
  INSTALL → pm_sd_list() → file picker → pm_install()
               reads .bin from SD in 512-byte chunks via FILE*
               writes to OTA via esp_ota_begin/write/end
  BACKUP  → pm_dump_to_sd()
               reads OTA via esp_partition_read()
               writes to SD, trims 0xFF padding

SD card: SPI bus — CS GPIO5, MOSI 23, MISO 19, SCLK 18
         Mount point: /sdcard
         Files: *.bin and *.purr listed from SD root
```

Blue LED (GPIO17) heartbeats at 500 ms while in the recovery UI.

---

## OTA Image Boot Sequence — Full OS (`cyd_s024c`)

```
ESP-IDF second-stage bootloader
  → reads otadata → loads ota_0 (or whichever slot pm_launch set)

main.cpp (pure IDF, no Arduino)
  → app_main() → kitt.init()
      1.  SPIFFS mount
      2.  device.json parse (ArduinoJson → device_config_t)
              on fail → emergency_text() + halt
      3.  Init display
              CYD:    display_ili9341_init()
              Heltec: display_ssd1306_init()
      4.  verbose or splash
      5.  wifi_manager_init()    (non-blocking background connect)
      6.  bt_manager_init()      (if PURR_HAS_BT) — restore NVS paired list
      7.  lora_manager_init()    (if PURR_HAS_LORA)
      8.  touch_cst816s_init()   (if PURR_HAS_TOUCH_CST816S)
      9.  power_manager_init()   — battery ADC, CPU freq from device.json
      10. partition_manager_init() (if PURR_HAS_PARTITION_MGR)
      11. apps_scan()     — scan SPIFFS /apps/
          firmware_scan() — scan SPIFFS /friends/
      12. nvs_set_u8("purr_bl", "boot_tries", 0)  ← clears crash-loop counter
      13. Spawn system_task (FreeRTOS core 1, priority 3)

system_task
  → bridge_start()
  → GPIO0 held LOW? → pm_boot_to_factory()   [never returns]
  → spawn MiniWin task (FreeRTOS core 0, priority 2)
  → launch shell:
        PURR_HAS_EXPLORER      → explorer_start()
        PURR_HAS_BLACKBERRY_UI → blackberry_ui_start()
        PURR_DISPLAY_SSD1306   → smol_start()
  → mpython_init() if BUILD_MINI=0
  → watchdog loop (vTaskDelay 5000ms, log RAM)
```

---

## Boot to Factory from Running OS

Any component can call:

```cpp
#include "modules/partition_manager.h"

pm_boot_to_factory();
// sets esp_ota_set_boot_partition(factory_partition)
// calls esp_restart() — never returns
```

Called automatically when GPIO0 is held LOW during `system_task` startup.

---

## Crash-Loop Protection

| Event | What happens |
|-------|-------------|
| Factory boots, ota_0 is valid PURR | Increments NVS `boot_tries`, chainloads ota_0 |
| KITT init succeeds in ota_0 | Clears NVS `boot_tries` to 0 |
| ota_0 crashes before KITT init | `boot_tries` never cleared; increments next boot |
| `boot_tries >= 3` | Factory shows SOS screen instead of chainloading |
| User taps "WIPE OTA 0" on SOS screen | Erases ota_0, clears counter, returns to home |
| User taps "BOOT ANYWAY" on SOS screen | Clears counter, chainloads once more |
| User taps "DISMISS" | Clears counter, opens normal recovery home |

---

## Boot Time Estimates

| Stage | Target | Typical time |
|-------|--------|-------------|
| ESP-IDF bootloader | all | ~200 ms |
| KITT init (display + modules) | CYD | ~800 ms |
| KITT init (display + modules) | Heltec | ~400 ms |
| WiFi reconnect (background) | — | 1–3 s (async) |
| MiniWin + shell render | CYD | ~300 ms |
| MicroPython init | CYD/Heltec | ~500 ms |
| **Total to shell ready** | **CYD** | **~1.5–2 s** |
| **Total to shell ready** | **Heltec** | **~1–1.5 s** |
| Factory chainload to ota_0 | CYD | ~20 ms overhead |
