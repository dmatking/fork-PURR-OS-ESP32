# PURR OS — Boot Sequence
**v0.6.0**

---

## Overview

PURR OS has two distinct boot paths depending on which partition is active:

- **Factory image** (`cyd_boot`) — a minimal recovery bootloader. Scans OTA slots, shows a management UI.
- **OTA image** (`cyd` / `heltec` / `tdeck`) — the full OS with KITT, MiniWin, shells, MicroPython.

On CYD, the factory image is flashed **once** to `0x10000` and never touched by OTA. The full OS lives in `ota_0` at `0x110000` and is updated normally. The ESP-IDF `otadata` partition (at `0xe000`) records which slot to boot. After a fresh full-flash `otadata` defaults to factory, so `cyd_boot` runs first.

---

## CYD First Boot (after full flash)

```
ESP-IDF second-stage bootloader
  → reads otadata → points to factory (initial state)
  → loads factory image (cyd_boot)

cyd_boot
  → kitt.init()
      Serial.begin(115200)
      SPIFFS mount
      device.json parse
      display_ili9341_init()  + backlight ON
      touch_cst816s_init()
      partition_manager_init()
  → system_task
      bridge_start()
      purr_bootloader_start()
          [see Factory Boot Sequence below]
```

When the user taps BOOT on ota_0 from the bootloader UI:
```
purr_bootloader → pm_launch(0)
  → esp_ota_set_boot_partition(ota_0)
  → esp_restart()
  → ESP-IDF loads cyd full OS from ota_0
```

---

## Factory Boot Sequence — purr_bootloader_start()

```
1. pm_boot_slot()
      determine currently-active OTA slot (or -1 if factory)

2. Render home screen
      for each slot (ota_0, ota_1):
          esp_ota_get_partition_description(part, &desc)
          if valid image (magic byte 0xE9, size sane):
              draw slot card:
                  firmware name + version from desc
                  [BOOT] button  — TAG_BOOT(slot)
                  [WIPE] button  — TAG_WIPE(slot)
                  active badge if slot == pm_boot_slot()
          else (empty):
              draw slot card:
                  [INSTALL] button — TAG_INSTALL(slot)

3. Blue LED heartbeat — GPIO2, 500ms toggle

4. Touch event loop:
      tap TAG_BOOT(s)    → transition to PB_CONFIRM_BOOT
                            confirm → pm_launch(s) → esp_restart()
      tap TAG_WIPE(s)    → transition to PB_CONFIRM_WIPE
                            confirm → pm_delete(s) → refresh home
      tap TAG_INSTALL(s) → transition to PB_INSTALL_SELECT (future)
```

---

## OTA Image Boot Sequence — Full OS (cyd / heltec / tdeck)

```
ESP-IDF second-stage bootloader
  → reads otadata → loads ota_0 (or whichever slot was set)

main.cpp
  → kitt.init()
      1.  Serial.begin(115200)
      2.  SPIFFS mount
      3.  device.json parse (ArduinoJson → device_config_t)
              on fail → emergency_text() + halt
      4.  Init display
              CYD:    display_ili9341_init()
              Heltec: display_ssd1306_init()
      5.  Verbose or splash
              if verbose_boot: log to serial + display
              else: display_ili9341_clear() + boot splash text
      6.  wifi_manager_init()  (non-blocking, background connect)
      7.  bt_manager_init()    (if PURR_HAS_BT)
              restore paired device list from NVS
      8.  lora_manager_init()  (if PURR_HAS_LORA)
              on fail: log, continue; os_name stays "PUR OS"
      9.  touch_cst816s_init() (if PURR_HAS_TOUCH_CST816S)
      10. power_manager_init()
              read battery ADC1
              set CPU freq from device.json
      11. partition_manager_init() (if PURR_HAS_PARTITION_MGR)
      12. apps_scan()     — scan SPIFFS /apps/
          firmware_scan() — scan SPIFFS /friends/
      13. nvs_set_u32("kitt_hb", millis())  — first heartbeat
      14. Spawn system_task (FreeRTOS core 1, priority 3)

system_task
      bridge_start()
      check GPIO0 (CYD):
          if digitalRead(0) == LOW → pm_boot_to_factory() → never returns
      #ifdef PURR_IS_BOOTLOADER_IMG
          purr_bootloader_start()    ← never reaches here on OTA image
      #else
          [CYD] spawn MiniWin task (FreeRTOS core 0, priority 2)
          [CYD] launch shell based on PURR_SHELL flag:
              PURR_HAS_EXPLORER     → explorer_start()
              PURR_HAS_BLACKBERRY_UI→ blackberry_ui_start()
          [Heltec/T-Deck] smol_start()
          [if BUILD_MINI=0] mpython_init() — MicroPython runtime
      #endif
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

This is called:
- Automatically when GPIO0 is held LOW at system task startup (CYD)
- By future UI elements (Settings → Recovery / Bootloader)

---

## Watchdog

A FreeRTOS task reads `kitt_hb` from NVS every 1000ms. If the value is unchanged for 3000ms (KITT has stopped calling `update()`), it calls `esp_restart()`.

The watchdog only monitors after KITT writes `KITT_READY` to NVS — it does not interfere with early boot.

---

## Boot Time Estimates

| Stage | Target | Typical time |
|-------|--------|-------------|
| ESP-IDF bootloader | all | ~200ms |
| KITT init (display + modules) | CYD | ~800ms |
| KITT init (display + modules) | Heltec | ~400ms |
| WiFi reconnect (background) | — | 1–3s (async) |
| MiniWin + shell render | CYD | ~300ms |
| MicroPython init | CYD/Heltec | ~500ms |
| **Total to shell ready** | **CYD** | **~1.5–2s** |
| **Total to shell ready** | **Heltec** | **~1–1.5s** |
