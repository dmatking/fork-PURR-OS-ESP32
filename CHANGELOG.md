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
- `system/system/main.cpp` — shell launcher (calls `smol_start()` for ≤128px displays, `app_launch(explorer.meow)` for large); crash logger; memory threshold monitor; OTA staging via NVS boot flag
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

## [0.2.1] — 2026-05-25 — smol C++ Rewrite

### CoreOS — Built-in Apps (apps/)
- `apps/smol/smol.h/.cpp` — full C++ rewrite of the `smol.meow` text shell; no MicroPython dependency
  - 8-row × 16-char layout on 128×64 OLED: header, divider, 5-row scrollable app list, hint bar
  - UP/DOWN/SELECT navigation; double-SELECT to confirm launch; BACK opens PURR menu
  - PURR menu: About (device name, resolution, free RAM), System Info (RAM, CPU freq, uptime), Quit PURR OS (`esp_restart`)
  - Adaptive refresh: 150ms while active, 3s when idle >3s
  - Filters large-display-only apps (explorer, classicmac) from the list
  - Graceful "no runtime yet" message when `app_launch` fails (MicroPython binding still pending)
  - Child-process tracking: yields display and keys while a launched app is running

### CoreOS — KITT API addition
- `kitt.h` / `kitt.cpp` — added `get_key_event(generic_key_t*, bool*)`: reads injected generic keys directly from the ring buffer; intended for C++ shells that don't use LVGL

### CoreOS — System Layer update
- `system/system/main.cpp` — replaced `app_launch(smol.meow)` with `smol_start()` for ≤128px displays; large-display path unchanged

### Build
- `CMakeLists.txt` — added `apps/smol/smol.cpp` to SRCS; added `apps/smol` to INCLUDE_DIRS

---

## [0.2.2] — 2026-05-26 — LoRa rewrite (RAK3172 UART AT), board.md correction

### CoreOS — lora_manager rewrite
- `lora_manager.h` — replaced SPI pin defines with `LORA_UART_TX` / `LORA_UART_RX` / `LORA_UART_BAUD`; pins marked TBD pending PCB verification
- `lora_manager.cpp` — full rewrite from SPI (Heltec SX1262) to RAK3172 UART AT command protocol
  - P2P mode (`AT+NWM=0`); configures freq, SF, BW, CR, TX power via `AT+PFREQ/PSF/PBW/PCR/PTP`
  - Continuous RX window (`AT+PRECV=65535`); parses `+EVT:RXP2P:<rssi>:<snr>:<hex>` unsolicited events
  - yield: closes RX window + `AT+SLEEP`; reclaim: re-applies full P2P config
  - `lora_manager_update()` accumulates Serial2 bytes line-by-line for event parsing

### Docs
- `PURR_OS_docs/board.md` — corrected ESP32-S2 role (runs PURR OS, not HID-only); added note that UART2 connects to RAK3172 (pins TBD)

---

## [0.2.3] — 2026-05-26 — LoRa Kernels library

### LoRa Kernels (new top-level folder)
Drop-in lora_manager.h/.cpp pairs — identical public API, different radio backends.
Copy any folder's contents into `CoreOS/system/kernel/modules/` to switch radios.

| Folder | Radio | Interface | Notes |
|--------|-------|-----------|-------|
| `RAK3172/` | RAK3172 (STM32WL) | UART AT | P2P mode; active board target; TX/RX pins TBD |
| `SX1262/` | SX1262 | SPI | Heltec V3 pin defaults; requires arduino-LoRa |
| `SX1276_RFM95W/` | SX1276 / RFM95W | SPI | Generic breakout pin defaults; DIO0 not DIO1 |

All three kernels implement the same function set:
`init`, `update`, `deinit`, `enabled`, `set/get frequency/power/SF/BW/CR/sync`, `send`, `busy`, `data_available`, `read`, `yield`, `reclaim`, `yielded`

---

## [0.2.4] — 2026-05-26 — LoRa presence check, dynamic OS name

### CoreOS — KITT
- `kitt.h` — added `os_name()` to public API (115 methods total)
- `kitt.cpp` — `os_name_buf` defaults to `"PUR OS"`; upgraded to `"PURR OS"` at boot if LoRa init succeeds; logged either way
  - PUR OS = no radio / init failed
  - PURR OS = LoRa confirmed present and responding

### CoreOS — smol
- `smol.cpp` — desktop header and About screen now use `kitt.os_name()` instead of hardcoded `"PURR OS"`

---

## [0.3.0] — 2026-05-26 — MicroPython runtime + `import kitt`

### CoreOS — system/micropython/ (new)
- `mpython_runtime.h` — C-compatible header: runtime API (`mpython_init`, `mpython_exec_app`, process queries) + `extern "C"` KITT bridge function declarations
- `mpython_runtime.cpp` — C++ implementation
  - Process table (4 slots): each .meow app gets its own FreeRTOS task + `TaskHandle_t`
  - `mpython_exec_app(path)` — reads `<path>/main.py` from SPIFFS, launches into MicroPython task
  - `mpython_process_running/kill/ram_kb` — process lifecycle queries
  - All `c_kitt_*` bridge functions: thin `extern "C"` wrappers around the global `kitt` object (display, input, WiFi, LoRa, system, notifications)
  - MicroPython heap: 256 KB static (increase if PSRAM available)
- `kitt_module.c` — pure C MicroPython extension module; auto-registered via `MP_REGISTER_MODULE`
  - `import kitt` available to all .meow Python apps
  - Exports: `text_print`, `text_clear`, `display_width/height`, `os_name`, `device_name`, `poll_key`, `poll_key_pressed`, `KEY_*` constants, `wifi_connected/ssid/rssi/connect/disconnect`, `lora_enabled/available/rssi/send/read`, `free_ram`, `uptime_ms`, `battery_percent`, `cpu_mhz`, `notify`, `popup`

### CoreOS — KITT wired up
- `kitt.cpp` — `app_launch()` now calls `mpython_exec_app()`; `process_kill/running/ram_usage_kb` delegate to runtime; `mpython_init()` called at boot step 15
- `CMakeLists.txt` — added `system/micropython/mpython_runtime.cpp`, `kitt_module.c`; added `micropython` to REQUIRES

### Note
The `micropython` ESP-IDF component must be added before this compiles. Add to `idf_component.yml` or clone into `components/micropython`. Existing .meow apps use the old IPC pub/sub API and will need porting to `import kitt`.

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
