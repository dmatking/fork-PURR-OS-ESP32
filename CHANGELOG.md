# PURR OS Changelog

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
- All flash offsets updated in `Builder/Build.ps1` and `Builder/sdk_core.py`:
  - `cyd` → app at `0x110000` (ota_0), SPIFFS at `0x390000`
  - `cyd_boot` → app at `0x10000` (factory), SPIFFS at `0x390000`

### New build target: `cyd_boot` (factory recovery image)
- `CoreOS/main/CMakeLists.txt` — `cyd_boot` target block: overrides SRCS/REQUIRES to a minimal set (no WiFi, no BT, no OS modules, no MicroPython); forces `BUILD_MINI=1`
- Compile flags: `PURR_HAS_BOOTLOADER=1 PURR_IS_BOOTLOADER_IMG=1`
- `Builder/Build.ps1` — `cyd_boot` added as target [3]; chip = `esp32`; defaults fall back to `cyd.defaults`; app offset = `0x10000`
- `Builder/SDK.ps1` / `sdk_core.py` — `cyd_boot` added as selectable target with `fixed=True` (no module toggles)

### purr_bootloader — generic OTA slot scanner
- `CoreOS/system/kernel/modules/purr_bootloader.cpp` — complete rewrite (~300 lines):
  - Scans all OTA slots dynamically using `esp_ota_get_partition_description()` to read firmware version directly from flash
  - Per-slot button tags: `TAG_BOOT(s)`, `TAG_WIPE(s)`, `TAG_INSTALL(s)`
  - Screen states: `PB_HOME`, `PB_CONFIRM_WIPE`, `PB_CONFIRM_BOOT`, `PB_INSTALL_SELECT`, `PB_INSTALLING`
  - Valid slots: shows BOOT + WIPE buttons with firmware version; empty slots: shows INSTALL
  - Blue LED heartbeat in factory image mode
- `CoreOS/system/kernel/modules/purr_bootloader.h` — stripped to single declaration `purr_bootloader_start()`
- `CoreOS/system/system/main.cpp` — `PURR_IS_BOOTLOADER_IMG` path calls `purr_bootloader_start()`; OTA path checks GPIO0 and calls `pm_boot_to_factory()` instead of inline bootloader

### partition_manager — new APIs
- `CoreOS/system/kernel/modules/partition_manager.h` — two new declarations:
  - `pm_boot_slot()` — returns active OTA slot index (0/1/…) or -1 if booted from factory
  - `pm_boot_to_factory()` — sets factory as boot target via `esp_ota_set_boot_partition()` + `esp_restart()`
- `CoreOS/system/kernel/modules/partition_manager.cpp` — both implemented; `pm_delete()` updated to reset otadata to factory when the deleted slot was the active boot slot

### PURR_SHELL cmake variable
- `CoreOS/main/CMakeLists.txt` — `PURR_SHELL` cache variable added (`both | blackberry | explorer | smol | none`); default `both` preserves existing Build.ps1 behaviour
- `cyd` target: shell selection drives `PURR_HAS_BLACKBERRY_UI`, `PURR_HAS_EXPLORER` compile flags
- `tdeck` target: same, with `PURR_BBUI_TARGET_KEYS` instead of `PURR_BBUI_TARGET_TOUCH`

### Builder SDK — full rewrite
- `Builder/SDK.ps1` — new thin PS1 wrapper:
  - Auto-detects IDF `export.ps1` from candidate paths or `$env:IDF_EXPORT_PATH`
  - Maps all switches (`-Target`, `-Shell`, `-Build`, `-Flash`, `-Monitor`, `-Clean`, `-Mini`, `-NoBt`, `-Lora`, `-NoLora`, `-Mesh`, `-Mtp`, `-Flasher`, `-LoraKernel`, `-TdeckPlus`, `-Configure`, `-Baud`) to Python CLI args
  - Calls `python sdk_core.py @pyArgs`
- `Builder/sdk_core.py` — full Python SDK (380 lines):
  - Config: JSON in `Builder/purr_sdk.cfg`; persisted between runs
  - Interactive wizard: target picker → shell picker → module toggles (LoRa/mesh lock) → port entry
  - `_cmake_flags()` — builds full `-D` flag list including `PURR_SHELL`
  - Arduino patches ported from Build.ps1 (4 patches: ADC rename, I2C slave stubs, ESP_SR header, esp_timer REQUIRES)
  - `run_live()` — subprocess with live streaming output, KeyboardInterrupt → terminate
  - `do_build()`, `do_flash()` (per-target offsets), `do_monitor()`, `_build_spiffs()`
  - Main menu: `b/B/f/m/r/a/c/s/q` (`B` = clean build)
  - Non-interactive CLI via argparse
  - `_idf_py()` — resolves `idf.py` via `IDF_PATH/tools/idf.py` + `sys.executable` to fix Windows `FileNotFoundError` when calling bare `idf.py` via subprocess

### MiniWin window manager integration
- `CoreOS/components/miniwinwm/` — MiniWin (MIT) added as local IDF component (same pattern as TFT_eSPI):
  - `CMakeLists.txt` — GLOBs core `.c` sources from `MiniWin/`, excludes all upstream platform HAL dirs, adds PURR_CYD HAL `.cpp` files explicitly; defines `PURR_CYD=1`; only active for `cyd`/`cyd_boot` targets
  - `MiniWin/hal/PURR_CYD/miniwin_config.h` — 320×240 landscape, 20ms tick
  - `MiniWin/hal/PURR_CYD/hal_lcd.cpp` — `mw_hal_lcd_*` wired to `display_ili9341`; colour conversion `0x00RRGGBB → RGB565`; monochrome + colour bitmap clip via pixel-by-pixel fill (optimize later)
  - `MiniWin/hal/PURR_CYD/hal_touch.cpp` — `mw_hal_touch_*` wired to `touch_cst816s`; coordinates scaled `0..319/239 → 0..4095` for MiniWin calibration matrix; `is_recalibration_required` always `false` (CST816S is pre-calibrated capacitive)
  - `MiniWin/hal/PURR_CYD/hal_timer.cpp` — `esp_timer` 20ms periodic, increments `mw_tick_counter`
  - `MiniWin/hal/PURR_CYD/hal_delay.cpp` — Arduino `delay()` / `delayMicroseconds()`
  - `MiniWin/hal/PURR_CYD/hal_non_vol.cpp` — NVS blob store for MiniWin calibration data (namespace `miniwin`, key `mw_cal`)
- `CoreOS/main/CMakeLists.txt` — `miniwinwm` added to REQUIRES for `cyd` and `cyd_boot`

### UI shell direction
- `purr_explorer` — designated as Windows CE / PDA style shell: taskbar at bottom, overlapping movable windows, Start-menu app launcher, modal dialogs via MiniWin; implementation pending PDA design references
- `purr_blackberry_ui` — stays BB-style (fixed layout, status bar, app drawer) but will be rebuilt on MiniWin primitives instead of raw TFT calls

### Fixes
- `Builder/sdk_core.py` — `_idf_py()` helper: on Windows, bare `"idf.py"` in `subprocess.Popen` raises `FileNotFoundError` because Python scripts aren't executable by name; fix resolves full path via `$IDF_PATH/tools/idf.py` and invokes with `sys.executable`
- `CoreOS/system/kernel/modules/display_ili9341.h` — corrected CYD variant comment: active target is S024C (2.4" capacitive, CST816S, BL=GPIO27), not S028R

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
