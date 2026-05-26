# PURR OS Changelog

---

## [0.2.0] — 2026-05-25 — C++ CoreOS Rewrite + CattoHID

### Project Restructure
- Split repo into four top-level folders:
  - `CoreOS/` — C++ ESP-IDF kernel (new)
  - `CattoHID/` — ESP32-S2 HID firmware (new)
  - `Userland/` — Python .meow app bundles (kept)
  - `pre-rewrite/` — Original MicroPython, reference only (never deleted)
- Added `PURR_OS_docs/board.md` — token-efficient markdown conversion of 6-page KiCad schematic PDF
- Added `.gitignore` — excludes schematic PDF binary

### CoreOS — Kernel (system/kernel/)
- `device_config.h/.cpp` — parses device.json via ArduinoJson into `device_config_t`; handles display, radios array, flash size string, PSRAM, CPU freq
- `kitt.h` — full public API: lifecycle, display, input, WiFi, BT, LoRa, power, app/firmware management, memory stats, callbacks
- `kitt.cpp` — complete 20-step boot sequence; NVS heartbeat (500ms); LVGL keypad input driver; all 60+ API methods implemented
- `main.cpp` — Arduino entry point; calls `kitt.init()` then `system_start()`
- `device.json` + `devices/` — hardware profiles: heltec, cattopad, box3, ingenico

### CoreOS — Hardware Modules (system/kernel/modules/)

| Module | Notes |
|--------|-------|
| `display_ssd1306` | Adafruit SSD1306, row-based text, Heltec V3 target |
| `display_ili9488` | TFT_eSPI + LVGL double-buffer flush, backlight PWM, CattoPad target |
| `lora_manager` | SX1262 via Heltec LoRa lib; full config, TX/RX, yield/reclaim |
| `wifi_manager` | Async scan, connect, NVS credential persistence, yield/reclaim |
| `bt_manager` | Full API surface; NVS paired device list, discovery timer, yield/reclaim — BLE stack deferred to NimBLE integration |
| `power_manager` | ADC1 battery read, voltage→percent map, CPU freq scaling via `setCpuFrequencyMhz` |
| `touch_mxt336t` | I2C interrupt-driven, T100 message read, event struct |
| `pi_manager` | Gate/handshake state machine, UART halt sequence, display yield callbacks to kitt.cpp |
| `flasher` | OTA write via Arduino `Update.h`, NVS boot flag, display fallback for both display types |
| `mtp_manager` | Interface defined — tinyusb MTP class integration deferred |

### CoreOS — System Layer
- `system/bridge/main.cpp` — JSON keymap loader; raw GPIO→generic keycode translation; radio yield/reclaim brokering for /friends/ firmware
- `system/system/main.cpp` — shell launcher (picks smol.meow vs explorer.meow by display width); crash logger; memory threshold monitor; OTA staging via NVS boot flag
- `system/bridge/keymaps/heltec.json` — GPIO 0→SELECT, 47→BACK
- `system/bridge/keymaps/cattopad_4x5.json` — directional + action keys

### CoreOS — Boot
- `boot/watchdog/main.cpp` — FreeRTOS task; reads NVS heartbeat every 1s; triggers `esp_restart()` if stale >3s; waits for KITT_READY flag before monitoring
- `boot/emergency/main.cpp` — checks BOOT pin at power-on; minimal SPIFFS + display init; flashes `/recovery/recovery.bin` via OTA; 60s timeout then reboot
- `CMakeLists.txt` — ESP-IDF project; lists all sources; requires: arduino, lvgl, ArduinoJson, esp_wifi, bt, nvs_flash, spiffs, fatfs, esp_adc, app_update

### CattoHID (ESP32-S2 Pure HID Controller)
- `keyboard_matrix.h/.cpp` — 6×14 matrix scan (6 rows: IO6/IO14-18, 14 cols: IO21/IO13-7/IO0-5); 3-sample debounce; 5µs settle delay; just-pressed/just-released helpers
- `keymap.h` — 84-key QWERTY HID keycode table (HUT 1.4); modifier definitions with row/col/bit mapping
- `usb_hid.h/.cpp` — native USB HID via Arduino `USBHIDKeyboard`; 6KRO; report dedup (only sends on change)
- `main.cpp` — 1ms scan loop; builds HID report from debounced matrix; handles modifiers separately from key slots
- `CMakeLists.txt` — ESP-IDF build targeting ESP32-S2

### Userland — Python Apps (preserved)
All existing `.meow` bundles kept unchanged as porting reference:
- `explorer.meow` — Win95/CE desktop shell
- `explorer_lvgl.meow` — LVGL-accelerated CE shell
- `ClassicMac.meow` — Mac System 7/8 shell
- `finder.meow` — Mac-style filesystem browser
- `smol.meow` — text shell for 128×64 OLED (Heltec V3/V4)
- `purr_ui.meow` — tile grid launcher with drag-to-rearrange

---

## Pending / Next Steps

- **MicroPython→KITT bindings** — C extension modules so userland .meow apps can call KITT APIs; required before any Python app runs under CoreOS
- **BLE stack** — NimBLE-Arduino component wiring in `bt_manager.cpp`
- **MTP mode** — tinyusb MTP class for `mtp_manager.cpp` (drag-and-drop file access over USB)
- **CattoHID keymap verification** — row/col→physical key correspondence needs confirmed against PCB routing once board is in hand
- **First flash target** — Heltec V3 with `heltec.json`; minimum boot goal: SSD1306 splash + NVS heartbeat

---

## [0.1.0] — 2026-05-24 — Initial MicroPython Release

- Mac System 4/5 shell, three-tier UI, LoRa remote modules
- MicroPython kernel (NanoCore async supervisor, IPC pub/sub bus)
- Basic display, WiFi, LoRa, input drivers
- Explorer, ClassicMac, Finder, Smol, PURR UI apps
