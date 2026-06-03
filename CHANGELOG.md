# PURR OS Changelog

---

## [0.5.1] — 2026-05-31 — Build system fixes + IDF 5.1/5.2 compatibility

### Builder — cmake flag propagation fix
- `build.sh` / `Build.ps1` — cmake `-D` flags now passed to both `set-target` **and** `build`
  - Previously `idf.py set-target` ran cmake with default values (`BUILD_MINI=0`), causing it to
    attempt resolving the `micropython` component before the build step could override the flag
  - Both scripts now collect flags into an array and splat them onto every `idf.py` call

### CoreOS — CMakeLists.txt: MicroPython auto-fallback
- `main/CMakeLists.txt` — if the MicroPython submodule header is absent, build automatically
  falls back to mini mode with a CMake `WARNING` instead of a hard configure error
  - Prevents `micropython: unknown component` failures when the submodule hasn't been cloned

### Builder — sdkconfig defaults: FreeRTOS tick rate
- `targets/heltec.defaults` — added `CONFIG_FREERTOS_HZ=1000` (arduino-esp32 hard-requires 1000Hz)
- `targets/cyd.defaults` — same; applies to all arduino-esp32 targets

### IDF version constraint (arduino-esp32 3.0.0 compatibility matrix)
- `main/idf_component.yml` — IDF ceiling set to `<5.3.0`
  - IDF 6.x: `wifi_provisioning` component removed → cmake failure
  - IDF 5.3+: ESP32-S3 touch driver API replaced (`driver/touch_sensor.h` → `driver/touch_sens.h`,
    `touch_value_t` removed); arduino-esp32 3.0.0 uses the old API → compile failure
  - **IDF 5.1.x and 5.2.x are the only confirmed-working versions** with arduino-esp32 3.0.0
- `Builder/HOWTO.md` — Prerequisites §1 updated; troubleshooting table extended with touch and
  wifi_provisioning error → fix mappings

---

## [0.5.0] — 2026-05-30 — Builder SDK, per-module flags, sdk.py deprecated

### Builder — full rewrite (interactive + direct mode)
- `Builder/build.sh` (bash, Linux/macOS/Git Bash) — complete rewrite
  - No args → interactive wizard: device picker, LoRa kernel picker, numbered module toggle menu
  - Direct mode: `--target TARGET [--mini] [--clean] [--flash PORT] [--monitor PORT]`
  - Module flags: `--no-bt`, `--with-mtp`, `--with-flasher`, `--no-lora`, `--lora-kernel K`
  - `load_config` / `save_config` — persists choices to `purr_build.cfg` between runs
  - `install_lora_kernel` — copies selected backend from `LoRa Kernels/` into `CoreOS/system/kernel/modules/`
  - Sets `ARDUINO_SKIP_IDF_VERSION_CHECK=1` to bypass arduino-esp32's IDF version ceiling check
- `Builder/Build.ps1` (PowerShell 5.1+, Windows native) — new script; full feature parity with build.sh
  - All Unicode replaced with ASCII to avoid PS 5.1 Windows-1252 decode bug (`→` → `->`, etc.)
  - Params: `-Target`, `-Mini`, `-Clean`, `-Flash`, `-Monitor`, `-Setup`, `-LoraKernel`,
    `-NoBt`, `-WithMtp`, `-WithFlasher`, `-NoLora`
- `Builder/HOWTO.md` — new comprehensive build guide: prerequisites, per-target instructions,
  partition tables, flash/monitor workflows, troubleshooting table
- `.gitignore` — added `Builder/purr_build.cfg`
- `sdk.py` — **deprecated**; replaced by `build.sh` / `Build.ps1`

### CoreOS — CMakeLists.txt restructured for IDF 5.x
- `CoreOS/CMakeLists.txt` — stripped to 3-line project boilerplate
  (`idf_component_register` must live in a component subdirectory in IDF 5.x+)
- `CoreOS/main/CMakeLists.txt` — **new file**; all build logic moved here
  - Per-module CMake cache variables: `PURR_ENABLE_BT`, `PURR_ENABLE_LORA`, `PURR_ENABLE_MTP`, `PURR_ENABLE_FLASHER`
  - LoRa auto-enabled for `heltec` / `tdeck`; disabled for `cyd`
  - Managed component REQUIRES use `namespace__name` format: `espressif__arduino-esp32`,
    `bblanchon__arduinojson`, `lvgl__lvgl`
  - Device-specific source/include/REQUIRES selected via `TARGET_DEVICE`
  - `PURR_HAS_*` preprocessor defines emitted via `target_compile_definitions`
- `CoreOS/main/idf_component.yml` — moved from project root; dependencies unchanged

### CoreOS — KITT: optional Bluetooth
- `kitt.cpp` — added `#ifdef PURR_HAS_BT` guards in six locations:
  1. `bt_manager.h` include
  2. BT init block in `KITT::init()`
  3. `bt_manager_update()` call in `KITT::update()`
  4. `ts.bt_enabled` tray update (with `#else ts.bt_enabled = false`)
  5. `bt_manager_deinit()` in `KITT::shutdown()`
  6. All 19 BT API forwarding methods — full implementations when `PURR_HAS_BT`, no-op stubs otherwise

---

## [0.4.1] — 2026-05-29 — Multi-target build system

### Builder/ (new top-level folder)
Enter this folder and run `./build.sh` — it resolves `CoreOS/` automatically.

- `Builder/build.sh` — build wrapper; `--target heltec|cyd|tdeck`, `--mini`, `--clean`, `--flash PORT`, `--monitor PORT`
  - Validates `IDF_PATH`, warns on missing MicroPython submodule or TBD LoRa pin stubs
  - Copies `targets/<target>.defaults` → `CoreOS/sdkconfig.defaults`, cds into `CoreOS/`, runs `idf.py`
- `Builder/targets/heltec.defaults` — 8MB flash, 80MHz, `partitions_heltec.csv`, ESP32-S3 240MHz, debug log level
- `Builder/targets/cyd.defaults` — 4MB flash, 40MHz, `partitions_cyd.csv`, FatFS LFN, LVGL memory hint

### CoreOS — `partitions_heltec.csv` (new)
- 8MB Heltec layout: factory 2MB + ota_0 2MB + ota_1 2MB + spiffs ~1.9MB

### CoreOS — `CMakeLists.txt` (rewritten)
- Conditional on `TARGET_DEVICE` and `BUILD_MINI` CMake cache variables (set by `build.sh`)
- Heltec: compiles `display_ssd1306`, `lora_manager`, `smol`; no LVGL, no fatfs, no esp_partition
- CYD: compiles `display_ili9341`, `touch_xpt2046`, `partition_manager`, `launcher`; adds `lvgl`, `fatfs`, `esp_partition`
- T-Deck: placeholder stub (smol shell until BB6 shell lands)
- `--mini`: excludes `mpython_runtime.cpp` + `kitt_module.c` + `micropython` component; all shells still work

---

## [0.4.0] — 2026-05-29 — CYD target + standalone launcher OS

### New target: CYD (ESP32-2432S028R)
- `devices/cyd.json` — device profile: ILI9341 display, XPT2046 touch, WiFi+BT, 4MB flash

### CoreOS — display_ili9341 (new)
- `display_ili9341.h/.cpp` — ILI9341 driver; 320×240 landscape (rotation 1); LEDC backlight on GPIO21; double-buffer LVGL flush via TFT_eSPI

### CoreOS — touch_xpt2046 (new)
- `touch_xpt2046.h/.cpp` — XPT2046 resistive touch via software SPI (MOSI=32, MISO=39, SCLK=25, CS=33, IRQ=36); 4-sample averaging; calibrated to 320×240 landscape; registers as LVGL `LV_INDEV_TYPE_POINTER`

### CoreOS — partition_manager (new, replaces flasher for CYD)
- `partition_manager.h/.cpp` — multi-OTA partition manager
  - Scans `ota_0`…`ota_3` at boot; detects valid images by ESP32 magic byte (0xE9)
  - Per-slot names stored in NVS namespace `purr_pm`
  - `pm_launch(slot)` — `esp_ota_set_boot_partition()` + `esp_restart()`; never returns
  - `pm_install(slot, sd_path, name, cb)` — streams .bin from SD → `esp_ota_begin/write/end`; validates magic and size; progress callback for UI
  - `pm_delete(slot)` — erase + clear NVS entry
  - SD card on VSPI (CS=5, CLK=18, MOSI=23, MISO=19) at 20MHz

### CoreOS — launcher app (new)
- `apps/launcher/launcher.h/.cpp` — LVGL touch UI running on 320×240 CYD
  - Dark-themed card layout: one card per OTA slot
  - Occupied slot: firmware name, size, LAUNCH + DELETE buttons
  - Empty slot: + INSTALL button → file picker from SD root
  - Install screen: scrollable list of .bin/.purr files on SD; Flash button per file
  - Progress screen: bar + status label updated during flash via `pm_progress_cb_t`
  - Status bar: OS name, WiFi indicator, battery percent
  - Spawns own FreeRTOS task (core 1, priority 2)

### CoreOS — system layer
- `system/system/main.cpp` — added CYD detection: if `display == 320×240`, routes to `launcher_start()` instead of MicroPython explorer shell

### CoreOS — partition table
- `partitions_cyd.csv` — 4MB CYD layout: factory (1.25MB PURR OS), ota_0 (1.375MB), ota_1 (1MB), spiffs (320KB)

### CoreOS — KITT
- `kitt.cpp` — added `ili9341` display init branch (Step 5); added `xpt2046` touch init branch with LVGL pointer indev registration (Step 12)

### Build
- `CMakeLists.txt` — added `display_ili9341.cpp`, `touch_xpt2046.cpp`, `partition_manager.cpp`, `apps/launcher/launcher.cpp`; added `esp_partition` to REQUIRES

---

## [0.3.0] — 2026-05-25 — C++ CoreOS Rewrite + CattoHID

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
