# PURR OS Changelog

---

## [0.9.1] — 2026-06-07 — KITT 0.5.1 — MiniWin WCE shell: touch fix, clickable start menu

### MiniWin HAL touch fix
- `hal_touch.cpp` — `mw_hal_touch_get_point()` now calls `mw_hal_touch_get_state()` internally to poll fresh CST816S data before returning coordinates, matching the reference HAL pattern. Previously the static `s_ev` cache was never refreshed during normal MiniWin operation (MiniWin calls `get_point` directly, never `get_state`), so touch was silently ignored.

### Windows CE shell (`purr_app.cpp`)
- Replaced multi-window taskbar approach with a single full-screen window (`MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT`) — all UI drawn in one paint callback, eliminating window focus and z-order issues
- `mw_paint_all()` called at end of `mw_user_init()` to clear calibration screen artifact on first boot
- Start button renders sunken (`draw_sunken`) while start menu is open, raised otherwise
- Start menu items now functional: 60ms press highlight (navy/white), then action fires via 3-tick timer
  - "About PURR OS" opens a centered about dialog showing version, device, free RAM; tap anywhere to close
  - "Shut Down..." calls `esp_restart()`
  - "Programs" / "Settings" close the menu (stubs for future use)
- Added `about_open` overlay state drawn directly in the shell paint function
- `MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT` added to all interactive windows (required for non-focused windows to receive touch events)

### SDK / version
- Version bumped to 0.9.1 / KITT 0.5.1 across `CMakeLists.txt`, `purr_version.h`, `sdk_core.py`, and about dialog string

---

## [0.9.0] — 2026-06-07 — KITT 0.5.0 — Arduino removed, pure IDF, SD recovery environment

### Arduino dependency removed (lib_arduino dropped)
- Removed `lib_arduino` as a standalone component from all targets and all REQUIRES lists
- All driver source files ported to pure ESP-IDF APIs:
  - `drv_bt/bt_manager.cpp` — replaced `millis()` with `esp_timer_get_time() / 1000`
  - `drv_gps/gps_manager.cpp` — same millis replacement
  - `drv_display/display_ili9341.cpp` — replaced Arduino LEDC API with `ledc_timer_config` / `ledc_channel_config` / `ledc_set_duty` / `ledc_update_duty`
  - `drv_hid/keyboard_matrix.cpp` — replaced `pinMode/digitalWrite/digitalRead/delayMicroseconds` with `gpio_set_direction/gpio_set_level/gpio_get_level/esp_rom_delay_us`
  - `drv_hid/hid_main.cpp` — rewrote `setup()/loop()` to IDF FreeRTOS task pattern
  - `drv_touch/touch_mxt336t.cpp` — replaced Wire.h with IDF `i2c_master_transmit/receive`; replaced `attachInterrupt` with `gpio_isr_handler_add`
  - `drv_lora/kernels/*/lora_manager.cpp` — replaced `millis()` with IDF timer
  - `drv_display/display_st7789.cpp`, `display_st7796.cpp`, `display_ili9488.cpp` — removed unused `purr_idf_compat.h` includes
- Redundant `#ifdef PURR_HAS_*` guards removed from driver .cpp files (CMakeLists already handles conditional compilation)

### TFT_eSPI — self-contained minimal Arduino shim
- `CoreOS/components/lib_tftespi/Arduino.h` — minimal shim containing only what TFT_eSPI needs:
  - Timing: `millis()`, `micros()`, `delay()`, `delayMicroseconds()`
  - GPIO: `pinMode()`, `digitalWrite()`, `digitalRead()`, `yield()`
  - Math: `random()`, `min/max`, `constrain`, `ltoa()`
  - PROGMEM: no-op macros for `pgm_read_*`
  - `SPIClass` stub + `extern SPIClass SPI`
  - `String` class with `toCharArray`, `indexOf`, `substring`, `+=`
- `CoreOS/components/lib_tftespi/Print.h` — minimal `Print` base class for TFT_eSPI
- `CoreOS/components/lib_tftespi/SPI.h` — thin wrapper including Arduino.h
- `CoreOS/components/lib_tftespi/arduino_compat.cpp` — defines `SPIClass SPI` global
- `lib_tftespi/CMakeLists.txt` — compiles `TFT_eSPI.cpp` + `arduino_compat.cpp`; `INCLUDE_DIRS "." "Extensions"`

### IDF component structure (drv_* components)
All hardware drivers moved from `system/kernel/modules/` monolith into discrete IDF components under `CoreOS/components/`:
- `drv_display/` — ILI9341, ST7789, ST7796, SSD1306, ILI9488
- `drv_touch/` — CST816S, XPT2046, GT911, MXT336T
- `drv_bt/` — Bluetooth manager
- `drv_gps/` — GPS UART manager
- `drv_hid/` — USB HID keyboard matrix
- `drv_lora/kernels/` — LoRa backends (SX1262, SX1276, RAK3172)
- `drv_wifi/` — WiFi manager

### IDF include propagation workarounds
IDF v5.3 does not propagate `INTERFACE_INCLUDE_DIRECTORIES` through `REQUIRES` for some component pairs. Applied `idf_component_get_property(lib <name> COMPONENT_LIB)` + `target_link_libraries(PRIVATE ${lib})` workaround in:
- `drv_touch/CMakeLists.txt` — for `esp_driver_gpio`, `esp_driver_i2c`, `esp_driver_spi`
- `drv_display/CMakeLists.txt` — for `esp_driver_ledc`
- `main/CMakeLists.txt` (cyd_boot) — for `fatfs`, `sdmmc`

### WiFi stub for heltec
- `drv_wifi/wifi_manager_stub.cpp` — no-op implementations of all `wifi_manager_*` functions; compiled for heltec in place of the real WiFi stack
- `drv_wifi/CMakeLists.txt` — exposes `INCLUDE_DIRS "."` even for stub build so `kitt.cpp` can include `wifi_manager.h` unconditionally

### purr_wm.h include guard
- `system/system/main.cpp` — `#include "purr_wm.h"` wrapped in `#ifdef PURR_HAS_MINIWIN`; `cyd_boot` does not have MiniWin and no longer fails to find the header

### SD card recovery environment in factory partition (`cyd_boot`)
Previously `cyd_boot` used `partition_manager_stubs.cpp` with no-op SD functions. SD support is now fully live in the factory partition:
- `main/CMakeLists.txt` (cyd_boot) — switched from `partition_manager_stubs.cpp` to `partition_manager.cpp`; added `fatfs` and `sdmmc` to REQUIRES
- `partition_manager.cpp` — SD card init (`sdspi`, CS GPIO5, MOSI 23, MISO 19, SCLK 18); `pm_sd_list()` returns `.bin` / `.purr` files from SD root; `pm_install()` writes SD file to OTA slot via `esp_ota_begin/write/end`; `pm_dump_to_sd()` reads OTA partition and writes a trimmed binary to SD
- Bug fix: `pm_sd_list()` was storing full mount paths in `files[].path` while `pm_install()` was prepending the mount prefix again — resulting in double path (`/sdcard//sdcard/file.bin`); fixed to store bare filenames in `path`
- Bug fix: `purr_bootloader.cpp` backup path `"/PURR_BACKUP_OTA0.bin"` had a spurious leading slash; removed
- `partition_manager.cpp` — `dev_cfg.gpio_cs` cast to `gpio_num_t` (strict C++ type check)

### Build sizes (v0.9.0)

| Target | Binary | Partition | Free |
|--------|--------|-----------|------|
| `cyd_s024c` | 831 KB | ota_0 1.5 MB | 20% |
| `cyd_boot` | 394 KB | factory 1 MB | 62% |
| `heltec` | 269 KB | factory 2 MB | 74% |

### Version bumps
- `purr_version.h` — `PURR_OS_VERSION` 0.8.0 → 0.9.0; `KITT_VERSION` 0.4.1 → 0.5.0
- `CoreOS/CMakeLists.txt` — `PROJECT_VER` 0.7.0 → 0.9.0

---

## [0.7.0] — 2026-06-04 — KITT 0.4.0 — PURR Kernel, factory chainload, SOS recovery, new targets

### PURR Kernel — factory partition redesign
- `CoreOS/system/system/main.cpp` — factory image now acts as a smart kernel, not just a passive recovery shell:
  - Reads `esp_app_desc_t` from ota_0 at boot; if `project_name == "purr_os_core"`, chainloads immediately (~20ms delay)
  - **Crash-loop detection**: increments `purr_bl/boot_tries` NVS key before each chainload; ota_0 clears it on successful KITT init; ≥3 consecutive failed boots → SOS mode
  - **GPIO 0 held at power-on** → forces bootloader UI regardless of ota_0 contents
  - Non-PURR firmware in ota_0 → falls through to bootloader UI (pure passthrough mode)
- `CoreOS/system/kernel/kitt.cpp` — clears `purr_bl/boot_tries` counter at end of successful `KITT::init()`

### SOS recovery mode
- `CoreOS/system/kernel/modules/purr_bootloader.cpp` — new `PB_SOS` screen state:
  - Red alert header with consecutive crash count
  - **WIPE OTA 0** → confirm wipe flow; **BOOT ANYWAY** → clears counter + chainloads; **DISMISS** → clears counter + opens normal bootloader home
- `purr_bootloader_start(bool sos, uint8_t boot_tries)` — factory kernel passes crash state in; `PB_SOS` is the initial screen when crash loop detected
- `purr_bootloader.h` — added `#include <stdint.h>` (fixes `uint8_t` declaration error)

### Firmware backup + restore flow
- `CoreOS/system/kernel/modules/partition_manager.cpp` — new `pm_dump_to_sd(slot, path, cb)`:
  - Reads OTA partition in 512-byte chunks via `esp_partition_read()`; writes to SD; trims trailing `0xFF` padding so the dump is a clean app image
  - Mirror of `pm_install()` — the backup can be directly reflashed
- `CoreOS/system/kernel/modules/partition_manager.h` — `pm_dump_to_sd` declared
- `CoreOS/system/kernel/modules/purr_bootloader.cpp` — new screens and flow:
  - `PB_CONFIRM_BACKUP` — offered before overwriting a slot that contains PURR firmware
  - `PB_BACKING_UP` — progress screen for SD dump
  - `PB_POST_INSTALL` — after any install: **RESTORE PURR** (reflash from backup + launch) or **BOOT NEW FW** (warn + launch)
  - `PB_RESTORING` — progress screen for restore
  - INSTALL action checks `esp_app_desc_t` of the target slot; if PURR firmware + SD present → backup offered automatically

### Factory kernel linker stubs
- `CoreOS/system/kernel/modules/stub_managers.cpp` — empty stub implementations of all `wifi_manager_*` and `power_manager_*` symbols; compiled only for `PURR_IS_BOOTLOADER_IMG`; satisfies linker without pulling wifi stack into the factory image

### Build system — full CYD build by default
- `Builder/sdk_core.py` — menu redesign for CYD targets:
  - `[b]` / `[r]` / `[a]` / `[s]` now run `do_full_build` (kernel + userland) instead of single-target build
  - `[B]` — same but clean
  - `[k]` — new option: build kernel (factory) only
  - WIP target banner tag `[WIP]` shown in yellow for jc3248w535 / waveshare169
- `Builder/Build.ps1` — per-target build directories (`build_cyd`, `build_cyd_boot`, etc.) and per-target `sdkconfig_<target>`; `-B` and `-DSDKCONFIG` now passed correctly to all `idf.py` calls; monitor call gets `-B $buildDirName`
- Port prompting: `do_flash`, `do_monitor`, `do_full_flash` now prompt for port interactively if none configured, and save the answer; `do_monitor` falls back to `flash_port` if `monitor_port` is empty
- `sdk_core.py` — `cyd_boot` display label → `cyd [PURR Kernel]`; Full Build/Flash descriptions updated to kernel/userland terminology

### WIP targets: JC3248W535 + Waveshare 1.69"
- New display drivers: `display_st7796.cpp/.h` (480×320, ESP32-S3), `display_st7789.cpp/.h` (240×280, ESP32-S3)
- New touch driver: `touch_gt911.cpp/.h` — GT911 5-point I2C; I2C address 0x5D (flippable to 0x14); hard-reset sequence; buffer-clear on read
- Device JSONs: `jc3248w535.json`, `waveshare169.json`
- sdkconfig.defaults: ESP32-S3, QIO flash, OPI PSRAM (jc3248w535), USB-OTG
- Partition tables: `partitions_jc3248w535.csv` (16MB — 2MB factory, 6MB×2 OTA, ~2MB SPIFFS), `partitions_waveshare169.csv` (4MB — mirrors CYD layout)
- `CoreOS/main/CMakeLists.txt` — target blocks for both; `PURR_TARGET_WIP=1`; forced mini build; FATAL_ERROR if BlackberryUI requested but MiniWin not cloned
- `CoreOS/system/kernel/kitt.cpp` — display init, `text_print`, `text_clear`, `text_set_color`, touch init wired for `st7796`, `st7789`, `gt911`
- `Builder/build_jc3248w535.ps1`, `Builder/build_waveshare169.ps1`

### MiniWin build fixes (cyd)
- `CoreOS/components/miniwinwm/CMakeLists.txt`:
  - HAL filter changed from `.*/hal/.*\.c$` to `.*/hal/[^/]+/.+\.c$` — was accidentally excluding `hal/hal.c` (which defines `mw_hal_init`), causing linker failure
  - MiniWin include dirs now explicitly listed in `INCLUDE_DIRS` for the cyd main component to work around IDF 5.3.x propagation bug
- `CoreOS/main/CMakeLists.txt` — added `components/miniwinwm/MiniWin`, `hal`, `hal/PURR_CYD` to cyd `INCLUDE_DIRS`; FATAL_ERROR if BlackberryUI requested without MiniWin source cloned

---

## [0.6.1] — 2026-06-04 — MajorasMask — Build system hotfixes, MiniWin CYD port

### IDF 5.3.x managed-component include propagation fix (TFT_eSPI + miniwinwm)
- Root cause: IDF 5.3.x does not propagate include dirs from managed-component REQUIRES chains to local components. Every `Arduino.h`-dependent local component hit this.
- `CoreOS/components/TFT_eSPI/CMakeLists.txt` — replaced per-component `target_include_directories` with `file(GLOB IDF_ALL_COMPONENT_INCLUDES ...)` over all `components/*/include`; filters out host-only `linux/include` (shadows toolchain `sys/cdefs.h`) and `riscv/include`; adds arduino-esp32 `libraries/SPI/src` (not in `cores/esp32`)
- `CoreOS/components/miniwinwm/CMakeLists.txt` — same glob approach; also adds `system/kernel/modules` for `display_ili9341.h` / `touch_cst816s.h`
- Both components now activate for exactly the right targets: TFT_eSPI → `cyd` + `cyd_boot`; miniwinwm → `cyd` only

### arduino-esp32 REQUIRES patches extended
- `Builder/sdk_core.py` + `Builder/Build.ps1` — patch 4 now also adds `esp_driver_gpio` to arduino-esp32 REQUIRES (IDF 5.3.x split `driver/gpio.h` into `esp_driver_gpio` component); both `esp_timer` and `esp_driver_gpio` patched in one pass with guards to avoid double-patching

### miniwinwm compilation fixes
- `CoreOS/components/miniwinwm/CMakeLists.txt`:
  - Excluded `dialog_file_chooser.c` (requires user-supplied `app.h`)
  - Excluded truetype font DATA files (saves flash; PURR OS uses bitmap fonts only)
  - Added `MiniWin/gl/fonts/truetype/mcufont` to INCLUDE_DIRS for the mcufont renderer
  - Guard changed to `cyd` only — `cyd_boot` draws directly to display; MiniWin would require unimplemented `mw_user_*` stubs and adds unnecessary code size
- `MiniWin/hal/PURR_CYD/miniwin_config.h` — completely rewritten: was 4 lines, now has all ~30 required constants (`MW_ROOT_WIDTH/HEIGHT`, `MW_MAX_WINDOW_COUNT`, `MW_CONTROL_UP_COLOUR`, `MW_CURSOR_PERIOD_TICKS`, all colour/timing/font defs)
- `MiniWin/hal/PURR_CYD/hal_delay.cpp` — removed `#include <Arduino.h>`; replaced `delay()`/`delayMicroseconds()` with `vTaskDelay(pdMS_TO_TICKS(ms))` / `ets_delay_us(us)` (eliminates Arduino.h dependency chain from miniwinwm)
- `MiniWin/hal/PURR_CYD/hal_timer.cpp` — added `#include <stdint.h>` (hal_timer.h uses `uint32_t` without including it); fixed `mw_tick_counter++` → explicit `= mw_tick_counter + 1` (C++20 deprecates `++` on `volatile`, treated as error by `-Werror=all`)

### blackberry_ui — full MiniWin root window port
- `CoreOS/system/kernel/modules/blackberry_ui.cpp` — complete rewrite (~560 lines):
  - RGB565 colour palette kept; `c565()` inline converts to `mw_hal_lcd_colour_t` (0x00RRGGBB) at draw time
  - All `display_ili9341_*` draw calls replaced with `mw_gl_*` equivalents; `mw_gl_draw_info_t*` threaded through every helper (`fill`, `hline`, `str`, `str_cx`)
  - `str_cx()` now uses `mw_gl_get_string_width_pixels()` instead of `strlen * 6 * sz`
  - Touch polling removed; touch arrives as `MW_TOUCH_DOWN/DRAG/UP_MESSAGE` in `mw_user_root_message_function()`
  - Implements all three required MiniWin user callbacks: `mw_user_init`, `mw_user_root_paint_function`, `mw_user_root_message_function`
  - `blackberry_ui_start()` spawns MiniWin FreeRTOS task (12 KB stack, core 1, priority 3); task drains message queue each 20ms tick

### Other fixes
- `CoreOS/system/kernel/modules/explorer.cpp` — `purr_bootloader_request_reboot()` → `pm_boot_to_factory()`; include updated to `partition_manager.h`
- `CoreOS/system/kernel/modules/flasher.cpp` — removed dead `#include "display_ili9488.h"`; SSD1306 calls guarded with `#ifdef PURR_DISPLAY_SSD1306`; ILI9341 path added with `#ifdef PURR_DISPLAY_ILI9341` — fixes linker errors on CYD target where `display_ssd1306_*` symbols are absent
- `CoreOS/main/CMakeLists.txt` (cyd_boot) — removed `miniwinwm` from REQUIRES
- `README.md` — added **Requirements** section: IDF v5.3.x is the only supported version (arduino-esp32 3.1.x pins `>=5.3,<5.4`); notes on Windows install path and automatic patch application

### Windows MiniWin simulator (sim/)
- `sim/` — new standalone CMake project for testing the MiniWin UI layer on Windows without hardware:
  - `main.c` — Win32 WinMain; creates 320×240 client-area window; routes mouse → MiniWin touch messages
  - `hal_lcd_sim.c` — MiniWin Windows HAL patched for 320×240 landscape (upstream is 240×320 portrait)
  - `mw_user.c` — self-contained BB homescreen using only `mw_gl_*`; stub app list; swipe/tap navigation
  - `miniwin_config.h` — 320×240 landscape, matches PURR_CYD
  - `app.h` — declares `hwnd`, `mx`, `my`, `mouse_down`, `app_main_loop_process()` for Windows HAL
  - `build.ps1` — build + run script; auto-detects MinGW or MSVC via cmake
  - **Requires**: cmake + MinGW or Visual Studio (not bundled with IDF)

---

## [0.6.0] — 2026-06-03 — Partition split, factory bootloader, MiniWin WM, SDK rewrite

### Partition layout redesign (CYD)
- `CoreOS/partitions_cyd.csv` — complete rework:
  - `factory` shrunk to 1 MB — now a dedicated bootloader-only slot, never touched by OTA
  - `ota_0` grown to 1.5 MB — full PURR OS image lives here
  - `ota_1` at 1 MB — generic third-party slot
  - `spiffs` at `0x390000`, 448 KB
- All flash offsets updated in `Builder/Build.ps1` and `Builder/sdk_core.py`

### New build target: `cyd_boot` (factory recovery image)
- `CoreOS/main/CMakeLists.txt` — `cyd_boot` target block: overrides SRCS/REQUIRES to a minimal set (no WiFi, no BT, no OS modules, no MicroPython); forces `BUILD_MINI=1`
- Compile flags: `PURR_HAS_BOOTLOADER=1 PURR_IS_BOOTLOADER_IMG=1`

### purr_bootloader — generic OTA slot scanner
- Scans all OTA slots dynamically using `esp_ota_get_partition_description()` to read firmware version directly from flash
- Screen states: `PB_HOME`, `PB_CONFIRM_WIPE`, `PB_CONFIRM_BOOT`, `PB_INSTALL_SELECT`, `PB_INSTALLING`
- Blue LED heartbeat in factory image mode

### partition_manager — new APIs
- `pm_boot_slot()` — returns active OTA slot index or -1 if booted from factory
- `pm_boot_to_factory()` — sets factory as boot target + esp_restart()

### Builder SDK — full rewrite
- `Builder/SDK.ps1` — thin PS1 wrapper; auto-detects IDF `export.ps1`; calls `sdk_core.py`
- `Builder/sdk_core.py` — full Python SDK (380 lines): interactive wizard, config persistence, cmake flag builder, arduino patches, build/flash/monitor

### MiniWin window manager integration
- `CoreOS/components/miniwinwm/` — MiniWin (MIT) added as local IDF component
- PURR_CYD HAL: `hal_lcd`, `hal_touch`, `hal_timer`, `hal_delay`, `hal_non_vol` wired to ILI9341 + CST816S

---

## [0.5.1] — 2026-05-31 — Build system fixes + IDF 5.1/5.2 compatibility

- cmake `-D` flags now passed to both `set-target` and `build`
- MicroPython auto-fallback to mini mode if submodule absent
- `CONFIG_FREERTOS_HZ=1000` added to all arduino-esp32 targets
- `idf_component.yml` IDF ceiling set to `<5.3.0` (arduino-esp32 3.0.0 compatibility)

---

## [0.5.0] — 2026-05-30 — Builder SDK, per-module flags

- `Builder/build.sh` — interactive wizard + direct mode; module flags; config persistence
- `Builder/Build.ps1` — PowerShell parity
- `CoreOS/main/CMakeLists.txt` — per-module CMake cache variables: `PURR_ENABLE_BT/LORA/MTP/FLASHER`
- `kitt.cpp` — `#ifdef PURR_HAS_BT` guards across all 6 call sites + 19 API methods

---

## [0.4.1] — 2026-05-29 — Multi-target build system

- `Builder/build.sh` — `--target heltec|cyd|tdeck`, `--mini`, `--clean`, `--flash`, `--monitor`
- `partitions_heltec.csv` — 8MB layout: factory 2MB + ota_0 2MB + ota_1 2MB + spiffs ~1.9MB
- `CMakeLists.txt` — conditional per-target source/include/REQUIRES selection

---

## [0.4.0] — 2026-05-29 — CYD target + standalone launcher OS

- CYD (ESP32-2432S028R) target: ILI9341, XPT2046, WiFi+BT, 4MB flash
- `display_ili9341.h/.cpp` — ILI9341 driver; LEDC backlight; double-buffer LVGL flush
- `touch_xpt2046.h/.cpp` — XPT2046 resistive touch; 4-sample averaging; 320×240 landscape
- `partition_manager.h/.cpp` — multi-OTA slot manager; SD card install; per-slot NVS names
- `apps/launcher/` — LVGL touch UI: slot cards, file picker, install progress, status bar

---

## [0.3.0] — 2026-05-25 — C++ CoreOS Rewrite + CattoHID

- Full C++/ESP-IDF kernel (KITT) with 20-step boot, 60+ APIs
- `device_config.h` — parses device.json via ArduinoJson
- Hardware modules: display_ssd1306, display_ili9488, lora_manager, wifi_manager, bt_manager, power_manager, touch_mxt336t, pi_manager, flasher, mtp_manager
- `CattoHID/` — ESP32-S2 HID firmware: keyboard matrix, HID report, USB

---

## [0.2.x] — 2026-05-25/26

- **0.2.1** — smol C++ rewrite (8-row OLED shell, UP/DOWN/SELECT navigation)
- **0.2.2** — LoRa rewrite (SX1262 → RAK3172 UART AT)
- **0.2.3** — LoRa Kernels library (SX1262 / RAK3172 / SX1276 drop-in backends)
- **0.2.4** — LoRa presence check; dynamic OS name (PUR OS / PURR OS)

---

## [0.1.0] — 2026-05-24 — Initial MicroPython Release

- Mac System 4/5 shell, three-tier UI, LoRa remote modules
- MicroPython kernel (NanoCore async supervisor, IPC pub/sub bus)
- Basic display, WiFi, LoRa, input drivers
- Explorer, ClassicMac, Finder, Smol, PURR UI apps
