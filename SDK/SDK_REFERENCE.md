# PURR OS SDK Reference

Complete reference for `sdk_core.py` — the build, flash, and configure tool for all PURR OS targets.

---

## Invocation

```bash
# Interactive menu (recommended for first use)
python3 SDK/sdk_core.py

# Direct build
python3 SDK/sdk_core.py --target tdeck_plus --build

# Build + flash in one shot
python3 SDK/sdk_core.py --target cyd_s028r --build --flash /dev/ttyUSB0

# PowerShell wrapper (Windows)
.\SDK\SDK.ps1
```

Settings are saved to `SDK/purr_sdk.cfg` (JSON) between sessions. You never need to re-enter your port or module choices unless you change targets.

---

## Prerequisites

### ESP-IDF

**Required version: 5.3.x** (currently tested on 5.3.5).

Source the environment before every build session:

```bash
. $IDF_PATH/export.sh
```

On Windows use the **ESP-IDF Command Prompt** shortcut installed by the Espressif Windows installer — it sources the environment automatically.

### Python dependencies

```bash
pip install pyserial    # optional but enables auto port detection
```

`esptool` is bundled with ESP-IDF and is invoked automatically during flash.

### No Arduino dependency

As of v0.10.1, Arduino-ESP32 has been fully stripped. PURR OS runs on **pure ESP-IDF 5.3+**. No `espressif__arduino-esp32` managed component is needed or fetched. All drivers use native IDF APIs.

---

## All CLI Arguments

### Target selection

| Argument | Values | Description |
|---|---|---|
| `--target` | See targets table below | Device to build for. Saved to config. |
| `--cyd-variant` | `s028r`, `s024c` | CYD display variant when target is `cyd`. `s028r` = original XPT2046 SPI touch. `s024c` = newer CST816S I2C touch. |
| `--tdeck-plus` | flag | Enables T-Deck Plus features (GPS, larger battery) when target is `tdeck`. Sets `BUILD_TDECK_PLUS=1`. |

### Build actions

| Argument | Description |
|---|---|
| `--build` | Compile the firmware for the current (or specified) target. |
| `--clean` | Delete the build directory and sdkconfig before building. Required when switching targets or after CMakeLists changes. |
| `--full-build` | CYD only: build `cyd_boot` (factory kernel) and `cyd` (OS) back-to-back. |

### Flash and monitor

| Argument | Values | Description |
|---|---|---|
| `--flash` | `PORT` or `auto` | Flash to a serial port after building. `auto` picks the first detected ESP32 device. |
| `--full-flash` | `PORT` | CYD only: flash bootloader + partition table + OS + SPIFFS in one `esptool` call. |
| `--monitor` | `PORT` or `auto` | Open serial monitor (idf.py monitor). Exit with `Ctrl+]`. |
| `--baud` | integer | Flash baud rate. Default: 460800. Reduce to 115200 if you get flash errors on cheap USB adapters. |
| `--scan` | flag | Scan for connected serial devices and print them, then exit. |

### Module toggles

These override the saved config for the current invocation. They are NOT automatically saved — use `--configure` to persist changes.

| Argument | CMake flag set | Description |
|---|---|---|
| `--no-wifi` | `PURR_ENABLE_WIFI=0` | Disable WiFi stack. Saves ~150 KB flash. |
| `--no-bt` | `PURR_ENABLE_BT=0` | Disable Bluetooth stack. Saves ~200 KB flash. |
| `--lora` | `PURR_ENABLE_LORA=1` | Enable LoRa radio driver. Only effective on hardware that has LoRa (heltec, tdeck, tdeck_plus). |
| `--no-lora` | `PURR_ENABLE_LORA=0` | Disable LoRa driver. |
| `--mesh` | `PURR_ENABLE_MESH=1` | Enable Meshtastic co-resident stack. Requires `--lora`. |
| `--mtp` | `PURR_ENABLE_MTP=1` | Enable MTP USB file transfer. |
| `--flasher` | `PURR_ENABLE_FLASHER=1` | Enable OTA partition flasher. |
| `--mini` | `BUILD_MINI=1` | Disable MicroPython runtime. Smaller binary; `.meow` scripts will not run. All other features unaffected. |
| `--gps` | `PURR_ENABLE_GPS=1` | Enable GPS manager (T-Deck Plus only, u-blox MIA-M10Q). |

### UI and shell

| Argument | Values | Description |
|---|---|---|
| `--ui-kernel` | `miniwin`, `none` | UI framework. `miniwin` = MiniWin window manager. `none` = headless / raw display. Saved to config. |
| `--lora-kernel` | `sx1262`, `rak3172`, `sx1276` | LoRa radio backend. See LoRa Kernels section below. Saved to config. |

### Configuration

| Argument | Description |
|---|---|
| `--configure` | Launch the interactive wizard to set target, modules, ports. Saves to `purr_sdk.cfg` and exits. |

---

## Target Reference

| Target key | Chip | Display | Touch | Flash | PSRAM | LoRa | Notes |
|---|---|---|---|---|---|---|---|
| `heltec` | ESP32-S3 | SSD1306 128x64 OLED | none | 8MB | none | SX1262 built-in | KittenUI text shell |
| `tembed_cc1101` | ESP32-S3R8 | ST7789 170x320 | none | 16MB | 8MB | CC1101 sub-GHz | Rotary encoder input |
| `cyd` | ESP32 | ILI9341 320x240 | depends on variant | 4MB | none | none | Alias — use `cyd_s028r` or `cyd_s024c` |
| `cyd_s028r` | ESP32 | ILI9341 320x240 | XPT2046 SPI | 4MB | none | none | Original CYD (ESP32-2432S028R) |
| `cyd_s024c` | ESP32 | ILI9341 320x240 | CST816S I2C | 4MB | none | none | Newer CYD variant (ESP32-2432S024C) |
| `cyd_boot` | ESP32 | ILI9341 320x240 | any | 4MB | none | none | Factory partition only — OTA-immune chainloader |
| `tdeck` | ESP32-S3 | ST7789 320x240 | none | 16MB | 8MB | SX1262 (external) | Trackball input, WIP |
| `tdeck_plus` | ESP32-S3 | ST7789 320x240 | GT911 I2C | 16MB | 8MB | SX1262 | Keyboard + trackball + optional GPS |
| `jc3248w535` | ESP32-S3 | ST7796 480x320 | GT911 I2C | 16MB | 8MB | none | 3.5" large display |
| `waveshare169` | ESP32-S3 | ST7789 240x280 | CST816S I2C | 4MB | none | none | 1.69" compact display |

### CYD variant notes

`cyd_s028r` and `cyd_s024c` map to the same `TARGET_DEVICE=cyd` in CMake but differ in:
- Touch driver: XPT2046 (s028r, SPI) vs CST816S (s024c, I2C)
- Backlight pin: GPIO21 (s028r) vs GPIO27 (s024c)
- The `CYD_DISPLAY_VARIANT` CMake variable and `CYD_VARIANT_S024C` preprocessor define control which driver is compiled

### T-Deck Plus vs T-Deck

Pass `--tdeck-plus` (or select Plus in the wizard) when targeting the T-Deck Plus. This sets:
- `BUILD_TDECK_PLUS=1` — gates GPS and larger-battery power code
- Enables the keyboard (I2C 0x55, SDA=18, SCL=8) and trackball (UP=3, DOWN=15, LEFT=1, RIGHT=2, CLICK=0) HAL
- Makes the `--gps` module available

---

## Module Reference

Modules are toggled in the interactive wizard or via CLI flags. Hardware constraints are enforced automatically — e.g. LoRa is silently stripped on devices with no radio hardware.

| Module key | CMake flag | Default | Supported targets | Flash cost | Description |
|---|---|---|---|---|---|
| `wifi` | `PURR_ENABLE_WIFI` | ON | all | ~150 KB | WiFi stack + HTTP server. Disable for air-gapped builds to reclaim RAM and flash. |
| `bt` | `PURR_ENABLE_BT` | ON | all | ~200 KB | Bluetooth BLE + Classic stack (`bt_manager`). |
| `lora` | `PURR_ENABLE_LORA` | ON (if hardware present) | heltec, tdeck, tdeck_plus | ~60 KB | LoRa radio driver. Automatically OFF on CYD, jc3248, waveshare, tembed_cc1101. |
| `mesh` | `PURR_ENABLE_MESH` | OFF | heltec, tdeck, tdeck_plus | ~300 KB | Meshtastic co-resident stack. Requires `lora=ON`. |
| `micropython` | `BUILD_MINI` (inverted) | ON | all | ~400 KB | MicroPython `.meow` app interpreter. `BUILD_MINI=1` disables it. |
| `shell` | `PURR_ENABLE_SHELL` | ON | all | ~30 KB | USB serial REPL (`drv_shell`). Commands: `gpio-set`, `display-color`, `display-text`, `reboot`, `mem`. |
| `lua` | `PURR_ENABLE_LUA` | ON | all MiniWin targets | ~120 KB | Lua 5.4 runtime for `.paws` / `.claw` scripts (`lib_lua`). |
| `mtp` | `PURR_ENABLE_MTP` | OFF | all | ~50 KB | USB MTP file transfer (`mtp_manager`). |
| `flasher` | `PURR_ENABLE_FLASHER` | OFF | all | ~20 KB | OTA partition flasher — allows writing firmware to ota_0/ota_1 from SPIFFS or SD. |
| `magidos` | `PURR_ENABLE_MAGIDOS` | OFF | CYD, tdeck_plus, jc3248 | ~500 KB | 8086 DOS emulator app (WIP). Runs `.COM` / `.EXE` files via 8086tiny + MiniWin UI. |
| `magicmac` | `PURR_ENABLE_MAGICMAC` | OFF | CYD, tdeck_plus, jc3248 | ~800 KB | Mac Plus emulator app (WIP). Requires umac + Musashi vendored and a Mac Plus ROM in SPIFFS. |
| `gps` | `PURR_ENABLE_GPS` | OFF | tdeck_plus | ~15 KB | u-blox MIA-M10Q GPS manager over UART. |

### Always-compiled components

These are unconditional and cannot be disabled via module toggles:

- `power_manager` — battery monitoring and sleep management
- `wifi_manager` is compiled based on the `wifi` module flag (now toggleable)
- `purr_catalog` — app registry and launcher
- `purr_taskbar` — taskbar / focus management (MiniWin targets)
- `device_config` — reads `device.json` from SPIFFS at boot

---

## LoRa Kernels

Selected with `--lora-kernel` or `[k]` in the module wizard. Only relevant when `lora=ON`.

| Key | Backend | Hardware | CMake file used |
|---|---|---|---|
| `sx1262` | RadioLib (IDF-native, no Arduino) | Heltec V3 built-in SX1262, T-Deck external, T-Deck Plus | `drv_lora/lora_manager.cpp` |
| `rak3172` | UART AT commands | RAK3172 module (CattoBoardV1 PCB) | `drv_lora/kernels/rak3172/lora_manager.cpp` |
| `sx1276` | RadioLib | Generic RFM95W breakout | `drv_lora/kernels/sx1276/lora_manager.cpp` |

The kernel is installed into `CoreOS/system/kernel/modules/` automatically by `sdk_core.py` before the build starts. You do not need to copy files manually.

### SX1262 pin configuration

Pins are defined per-target in `CoreOS/components/drv_lora/lora_manager.h`. Edit there for custom hardware:

| Target | CS | DIO1 | RST | BUSY | SCK | MOSI | MISO |
|---|---|---|---|---|---|---|---|
| Heltec V3 | 8 | 14 | 12 | 13 | 9 | 10 | 11 |
| T-Deck / T-Deck Plus | verify board silk | | | | | | |

---

## UI Kernels and Themes

### UI Kernels

| Key | CMake value | Description |
|---|---|---|
| `miniwin` | `PURR_UI_KERNEL=miniwin` | MiniWin window manager. Full touch-driven windowed OS with taskbar, Start menu, and app windows. Requires device HAL in `devices/<target>/`. |
| `none` | `PURR_UI_KERNEL=none` | No UI framework. Display is raw — your app owns it entirely. Used for KittenUI (OLED text shells) and headless devices. |

UI kernel is configurable for all MiniWin-capable targets: `cyd`, `cyd_s028r`, `cyd_s024c`, `tdeck_plus`, `jc3248w535`, `waveshare169`.

### MiniWin Themes

Only applies when `ui_kernel=miniwin`. Selected with `[t]` in the wizard.

| Key | CMake value | Description |
|---|---|---|
| `wce` | `PURR_UI_THEME=wce` | Windows CE-style gray desktop with raised/sunken borders, Start menu, taskbar. Default. |
| `blackberry` | `PURR_UI_THEME=blackberry` | Phosphor green-on-black terminal aesthetic. Tap the wallpaper to open the app drawer. |

---

## Arduino-ESP32 Integration

PURR OS uses `arduino-esp32` as a managed component (not as a board in Arduino IDE). This gives IDF components access to Arduino APIs (`Wire`, `SPI`, `Serial`, `SPI.begin()`, etc.) while the build system remains pure ESP-IDF.

### What this enables

- `Serial.print()` / `Serial.printf()` in any component that includes `Arduino.h`
- `SPI.begin()` / `Wire.begin()` — Arduino SPI and I2C wrappers
- `SPI` class used by RadioLib for LoRa hardware
- Arduino timing functions: `millis()`, `micros()`, `delay()`
- Arduino string class and stdlib helpers

### Automatic patches

`sdk_core.py` applies the following patches to the managed component after `idf.py set-target` populates `managed_components/`. These are idempotent and safe to re-apply:

| Patch | File patched | Reason |
|---|---|---|
| `adc_continuous_data_t` rename | `cores/esp32/esp32-hal-adc.h/.c` | IDF 5.x renamed this type; Arduino uses the old name |
| I2C slave LL stubs | `cores/esp32/esp32-hal-i2c-slave.c` | IDF 5.2+ removed internal LL functions that Arduino calls directly |
| `ESP_I2S.h` stub | `libraries/ESP_SR/src/ESP_I2S.h` | `ESP_SR` includes `ESP_I2S.h` from a sibling lib that isn't always present |
| CMakeLists REQUIRES patch | `espressif__arduino-esp32/CMakeLists.txt` | Adds `esp_timer` and `esp_driver_gpio` to Arduino's component REQUIRES so IDF 5.x header splits don't break the build |

These patches run automatically — you do not need to do anything. They are applied twice per build (after `set-target` and again just before `build`) to survive any CMake re-configuration.

### Disabling Arduino

There is currently no `--no-arduino` flag. Arduino-ESP32 is assumed present. If you need a pure-IDF build without Arduino, remove `espressif__arduino-esp32` from `CoreOS/idf_component.yml` and remove `AUTOSTART_ARDUINO` and `ARDUINO_SELECTIVE_COMPILATION` from the target's `.defaults` file. All components that `#include "Arduino.h"` will need alternative implementations.

### Arduino-ESP32 version pinning

The version is declared in `CoreOS/idf_component.yml`. To pin to a specific version:

```yaml
dependencies:
  espressif/arduino-esp32: "==3.1.0"
```

Current minimum: `>= 3.0.0`

---

## sdkconfig.defaults Files

Each target has a `SDK/targets/<target>.defaults` file that sets Kconfig options before the first build. The SDK copies it to `CoreOS/sdkconfig.defaults` automatically.

Key settings by category:

### Flash and partition table

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y       # 16MB flash (jc3248, tdeck_plus, etc.)
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y        # 4MB flash (CYD, waveshare)
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_<target>.csv"
```

### PSRAM (ESP32-S3 targets with OPI PSRAM)

```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y                # OPI mode (8MB variant)
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNALL=16384    # keep ≤16KB allocs in internal RAM
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768  # reserve 32KB internal for DMA
```

### CPU frequency

```
CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240=y   # 240MHz (ESP32-S3 targets)
CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y     # 240MHz (ESP32 targets)
```

### Arduino integration

```
CONFIG_AUTOSTART_ARDUINO=y              # run Arduino setup()/loop() — needed for arduino-esp32 init
CONFIG_ARDUINO_SELECTIVE_COMPILATION=y  # only compile Arduino libs actually used
```

### USB (ESP32-S3 targets)

```
CONFIG_USB_OTG_SUPPORTED=y
CONFIG_TINYUSB_ENABLED=y
```

### TFT_eSPI (CYD targets only)

TFT_eSPI pin configuration is read from `sdkconfig` via `TFT_config.h`. CYD defaults set these automatically. On all other targets `lib_tftespi` compiles as an empty stub — you never need to set these manually:

```
CONFIG_TFT_ILI9341_DRIVER=y
CONFIG_TFT_CS=15
CONFIG_TFT_DC=2
CONFIG_TFT_RST=-1
CONFIG_TFT_BL=21            # s028r; s024c uses 27
CONFIG_TFT_MOSI=13
CONFIG_TFT_SCLK=14
CONFIG_TFT_MISO=12
CONFIG_TFT_SPI_HOST=1       # HSPI
```

To edit sdkconfig options without wiping your build:

```bash
cd CoreOS
idf.py -B build_<target> menuconfig
```

---

## Config File (purr_sdk.cfg)

`SDK/purr_sdk.cfg` is written automatically by `--configure` and the interactive wizard. It is plain JSON and safe to edit by hand.

```json
{
  "target": "tdeck_plus",
  "shell": "kitten_ui",
  "lora_kernel": "sx1262",
  "ui_kernel": "miniwin",
  "ui_theme": "wce",
  "flash_port": "/dev/ttyUSB0",
  "flash_baud": 460800,
  "monitor_port": "/dev/ttyUSB0",
  "tdeck_plus": false,
  "cyd_variant": "s028r",
  "modules": {
    "wifi":        true,
    "bt":          true,
    "mtp":         false,
    "flasher":     false,
    "lora":        true,
    "mesh":        false,
    "micropython": true,
    "lua":         true,
    "magidos":     false,
    "magicmac":    false,
    "gps":         false
  }
}
```

CLI flags override the saved config for the current run but do not write back unless `--configure` is also passed.

---

## Build Directories

Each target gets its own isolated build directory so you can switch targets without a clean:

| Target | Build dir | sdkconfig |
|---|---|---|
| `heltec` | `CoreOS/build_heltec/` | `CoreOS/sdkconfig_heltec` |
| `cyd` / `cyd_s028r` / `cyd_s024c` | `CoreOS/build_cyd_s028r/` etc. | `CoreOS/sdkconfig_cyd_s028r` |
| `tdeck_plus` | `CoreOS/build_tdeck_plus/` | `CoreOS/sdkconfig_tdeck_plus` |
| `jc3248w535` | `CoreOS/build_jc3248w535/` | `CoreOS/sdkconfig_jc3248w535` |
| `waveshare169` | `CoreOS/build_waveshare169/` | `CoreOS/sdkconfig_waveshare169` |

To manually clean a single target without affecting others:

```bash
rm -rf CoreOS/build_tdeck_plus CoreOS/sdkconfig_tdeck_plus
```

---

## SPIFFS Image

After every successful firmware build, `sdk_core.py` automatically generates a SPIFFS image (`spiffs.bin`) using `spiffsgen.py` from the IDF. The image contains:

- `system/kernel/device.json` — hardware description for the running target (loaded by KITT at boot)

The image is flashed alongside the firmware when you use `--flash`. SPIFFS offset and size vary by target:

| Target family | SPIFFS offset | SPIFFS size |
|---|---|---|
| CYD (4MB flash) | `0x390000` | 448 KB |
| All others | `0x3b0000` | 320 KB |

To add files to the SPIFFS image, place them under `CoreOS/build_<target>/spiffs_staging/` before the build's SPIFFS step, or extend `_build_spiffs()` in `sdk_core.py`.

---

## Common Workflows

### First build for a new target

```bash
python3 SDK/sdk_core.py --target tdeck_plus --build --clean
```

### Build and flash

```bash
python3 SDK/sdk_core.py --target tdeck_plus --build --flash auto
```

### Build, flash, and open monitor

```bash
python3 SDK/sdk_core.py --target cyd_s028r --build --flash /dev/ttyUSB0 --monitor /dev/ttyUSB0
```

### Minimal binary (no WiFi, no BT, no MicroPython)

```bash
python3 SDK/sdk_core.py --target jc3248w535 --build --mini --no-wifi --no-bt
```

### Flash without rebuilding

```bash
python3 SDK/sdk_core.py --target cyd_s028r --flash /dev/ttyUSB0
```

### Scan for connected devices

```bash
python3 SDK/sdk_core.py --scan
```

### Save a new configuration

```bash
python3 SDK/sdk_core.py --target tdeck_plus --configure
# walks through wizard, saves purr_sdk.cfg
```

### T-Deck Plus with GPS

```bash
python3 SDK/sdk_core.py --target tdeck_plus --build --gps --flash auto
```

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `IDF_PATH not set` | Run `. $IDF_PATH/export.sh` (Linux/macOS) or open ESP-IDF Command Prompt (Windows) |
| `LoRa.h: No such file or directory` | The sx1262 kernel was using the old arduino-LoRa library. Fixed: root `lora_manager.cpp` is now the sole sx1262 source. |
| `Invalid Chip Select pin` from TFT_eSPI | `lib_tftespi` built for a non-CYD target. Fixed: CMakeLists now bails out early for non-CYD. |
| `PURR_HAS_PARTITION_MGR` linker errors | `partition_manager.cpp` not in SRCS for that target. Remove `PURR_HAS_PARTITION_MGR=1` from its define block in `CoreOS/main/CMakeLists.txt`. |
| `undefined reference to touch_cst816s_get_event` | CST816S guard missing. `drv_touch/CMakeLists.txt` must propagate `PURR_HAS_CST816S_TOUCH=1` as an INTERFACE define when cst816s.cpp is compiled. |
| CMake cache stale after CMakeLists changes | Delete the build dir: `rm -rf CoreOS/build_<target> CoreOS/sdkconfig_<target>` |
| Build works but nothing on screen | Check `PURR_UI_KERNEL` — must be `miniwin` for MiniWin targets. Verify sdkconfig copied correctly. |
| `wifi_provisioning: unknown name` | Wrong IDF version. PURR OS requires IDF 5.3.x. |
| Flash fails with `A fatal error occurred` | Reduce baud: `--baud 115200`. Some CH340 adapters are unstable at 460800. |
| Touch not responding on CYD | Calibrate: edit `X_RAW_MIN/MAX` and `Y_RAW_MIN/MAX` in `CoreOS/components/drv_touch/touch_xpt2046.cpp`. |
| T-Deck Plus keyboard not responding | Verify I2C bus: SDA=18, SCL=8, addr=0x55. Check if GT911 touch already claimed I2C_NUM_0 — `hal_input_init()` falls back to I2C_NUM_1. |
| `undefined reference to drv_8086_init` (or other drv_8086 symbols) when MAGIDOS enabled | GNU ld single-pass ordering issue: `lib_miniwin.a` is scanned twice and `app_magidos.cpp.obj` is pulled on the second pass, after `libdrv_8086.a` has already been scanned. Fix: compile `drv_8086.c` and `purr_dos_ipc.c` directly into `lib_miniwin` via its SRCS list (see `CoreOS/components/lib_miniwin/CMakeLists.txt`). Do NOT add drv_8086 as a separate component REQUIRES — it will end up in the wrong link position. |
| `.dram0.bss will not fit in region dram0_0_seg` when MAGIDOS enabled | 8086tiny's 640KB `mem[]` array lands in DRAM. Fix: pre-declare `mem[]` with `EXT_RAM_BSS_ATTR` in `drv_8086.c` before `#include "8086tiny.c"`, and guard the declaration in `8086tiny.c` with `#ifndef MEM_DECLARED_EXTERNALLY`. Requires `CONFIG_SPIRAM=y` and `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` in sdkconfig (set in `sdkconfig_tdeck_plus`). |
| `multiple definition of app_main` with Arduino component | `CONFIG_AUTOSTART_ARDUINO=y` in `sdkconfig.defaults` or target `.defaults` file tells Arduino to provide its own `app_main`. Set `CONFIG_AUTOSTART_ARDUINO=n` in `SDK/targets/<target>.defaults`. PURR OS provides its own `app_main` in `CoreOS/system/kernel/main.cpp`. |
