# PURR OS — v0.7.0

> **MEGA DISCLAIMER:** This project is very much vibe-coded. It has gone from 0 to "Jesus Christ" at an alarming rate. It is fully open-source and humans are actively encouraged to help. Please.

**P.U.R.R.** = Portable Unified Runtime & Radio Operating System
Powered by the **K.I.T.T** (Kernel Interface Translation Toolkit) kernel — a modular, C and C++ -backed embedded OS for ESP32 hardware.

---

## Versions

| Component | Version |
|-----------|---------|
| PURR OS   | v0.7.0  |
| KITT      | v0.4.0  |

Version strings are defined in [`CoreOS/system/kernel/purr_version.h`](CoreOS/system/kernel/purr_version.h) and automatically embedded into the firmware image via `esp_app_desc_t` — visible in the bootloader's slot card and on the homescreen.

---

## What is this?

PURR OS is an embedded operating system for ESP32 devices. It runs a custom kernel (KITT), exposes a MicroPython runtime for `.meow` apps, manages optional radio/BT/USB modules, and renders a full windowed UI via the **MiniWin** window manager. Think Windows CE vibes, on a $10 ESP32 LCD board.

The architecture splits across two flash partitions: a small **PURR Kernel** that lives in the factory slot (OTA-immune, handles boot decisions and recovery) and the **PURR Userland** in ota_0 that gets updated over-the-air.

---

## Supported Targets

| Target | Chip | Display | Touch | Status | Notes |
|--------|------|---------|-------|--------|-------|
| `cyd` | ESP32-2432S024C | ILI9341 2.4" 320×240 | CST816S cap | ✅ Active | Full OS → `ota_0` |
| `cyd_boot` | ESP32-2432S024C | ILI9341 2.4" 320×240 | CST816S cap | ✅ Active | PURR Kernel → `factory` |
| `heltec` | ESP32-S3 | SSD1306 OLED 128×64 | — | ✅ Working | WiFi + LoRa, Smol shell |
| `tdeck` | ESP32-S3 | ST7789 | trackball | 🚧 WIP | Shell pending |
| `jc3248w535` | ESP32-S3 | ST7796 3.5" 480×320 | GT911 cap | 🚧 WIP | Verify pins before flashing |
| `waveshare169` | ESP32-S3 | ST7789 1.69" 240×280 | CST816S cap | 🚧 WIP | Verify pins before flashing |

---

## Partition Layout (CYD, 4MB)

```
0x1000   IDF second-stage bootloader   ~27 KB   (immutable, flashed by esptool)
0x8000   Partition table                2 KB
0xe000   OTA data                       8 KB    (tracks active boot slot)
0x10000  factory   — PURR Kernel        1 MB    (OTA-immune, chainloads ota_0)
0x110000 ota_0     — PURR Userland      1.5 MB  (OTA-updatable)
0x290000 ota_1     — spare slot         1 MB    (third-party firmware / testing)
0x390000 spiffs    — filesystem         448 KB  (device config, app data, logs)
```

### Boot sequence

1. IDF bootloader reads OTA data → jumps to factory (PURR Kernel)
2. PURR Kernel reads `esp_app_desc_t` from ota_0 — if it's a PURR image, chainloads in ~20ms
3. If **GPIO 0 is held** at power-on → forces bootloader UI
4. If ota_0 has crashed 3 times in a row without clearing the counter → **SOS mode**
5. If ota_0 is empty or non-PURR firmware → bootloader UI (pure passthrough)

The PURR Kernel is **OTA-immune** — it can only be updated by flashing over USB. This means the device always has a recovery path regardless of what is in the OTA slots.

---

## PURR Kernel (factory partition)

The factory image is more than a recovery tool — it is the boot manager:

- **Fast-path chainload** — boots PURR userland in ~20ms on every normal power-on
- **Crash-loop detection** — KITT increments an NVS counter before each chainload and the userland clears it on successful init; 3 failures → SOS mode
- **SOS recovery screen** — wipe slot / boot anyway / dismiss
- **Bootloader UI** — lists all OTA slots with firmware version read direct from flash; per-slot Boot / Wipe / Install
- **Firmware backup** — before overwriting a slot that has PURR firmware, offers to dump it to SD card (`/PURR_BACKUP_OTA0.bin`); after install, offers to restore or boot new firmware
- **SD install** — flashes any `.bin` from SD card into any OTA slot using the IDF OTA API
- **Third-party firmware** — ota_1 slot can hold any ESP32 firmware; PURR Kernel acts as a pure passthrough bootloader for non-PURR images

---

## UI Shells (CYD — powered by MiniWin)

All CYD UI shells run on top of **[MiniWin](https://github.com/miniwinwm/miniwinwm)** (MIT licensed), wired to the ILI9341 and CST816S via a PURR-specific HAL port (`CoreOS/components/miniwinwm/MiniWin/hal/PURR_CYD/`).

| Shell | Flag | Description |
|-------|------|-------------|
| `blackberry` | `PURR_HAS_BLACKBERRY_UI` | BlackBerry OS 6-style — status bar, swipe-up app drawer, BB nav |
| `explorer` | `PURR_HAS_EXPLORER` | Windows CE / PDA style — taskbar, overlapping windows, Start launcher |
| `both` | both above | BlackberryUI preferred, Explorer as fallback (default for CYD) |
| `smol` | — | Minimal OLED shell (Heltec / T-Deck) |
| `none` | — | Headless — no UI shell compiled |

MiniWin runs as a FreeRTOS task (core 1, priority 3, 12KB stack). The root window callbacks (`mw_user_root_paint_function`, `mw_user_root_message_function`) are the homescreen. Apps open as borderless `mw_add_window()` windows on top.

> **Note:** MiniWin source must be cloned into `CoreOS/components/miniwinwm/` before building. A FATAL_ERROR is raised at cmake time if it is missing.

---

## Kernel Modules

| Module | CMake Flag | Default | Notes |
|--------|-----------|---------|-------|
| Bluetooth | `PURR_ENABLE_BT` | ON | BLE + Classic |
| LoRa Radio | `PURR_ENABLE_LORA` | ON (heltec/tdeck) | SX1262 / RAK3172 / SX1276 |
| Meshtastic | `PURR_ENABLE_MESH` | OFF | Requires LoRa |
| MTP USB | `PURR_ENABLE_MTP` | OFF | USB file transfer |
| OTA Flasher | `PURR_ENABLE_FLASHER` | OFF | In-app OTA flasher module |
| MicroPython | `BUILD_MINI=0` | ON (off for CYD WIP) | `.meow` app runtime |

---

## Build Tools

### SDK (recommended)

```powershell
# Interactive menu — target/shell/module/port selection persisted between runs
.\Builder\SDK.ps1

# Per-target scripts (same options, pre-set target)
.\Builder\build_cyd.ps1
.\Builder\build_cyd_boot.ps1
.\Builder\build_jc3248w535.ps1
.\Builder\build_waveshare169.ps1

# Direct build + flash
.\Builder\SDK.ps1 -Target cyd -Shell blackberry -Build -Flash COM8

# Clean full build (kernel + userland) + full flash
.\Builder\build_cyd.ps1 -FullBuild -Clean -FullFlash COM8
```

### Interactive menu keys (CYD targets)

| Key | Action |
|-----|--------|
| `b` | Full build — kernel + userland |
| `B` | Full clean build — kernel + userland |
| `k` | Kernel only build (factory partition) |
| `f` | Flash current target |
| `F` | Full flash — kernel + userland + SPIFFS in one esptool pass |
| `m` | Monitor (serial) |
| `r` | Build + Flash |
| `a` | Build + Flash + Monitor |
| `c` | Configure (target, shell, modules, ports) |
| `s` | Configure + Build |

### First flash (CYD — two images)

```powershell
# Recommended: build both and flash everything in one go
.\Builder\build_cyd.ps1 -FullBuild -Clean -FullFlash COM8

# Or manually:
.\Builder\build_cyd_boot.ps1 -Build -Flash COM8   # factory kernel (flash once)
.\Builder\build_cyd.ps1 -Build -Flash COM8         # userland (OTA-updatable)
```

---

## Project Structure

```
PURR-OS/
│
├── Builder/                    — Build tooling (run from here)
│   ├── SDK.ps1                 — Main entry point: sources IDF, calls sdk_core.py
│   ├── sdk_core.py             — Python SDK: interactive menu, build/flash/monitor
│   ├── Build.ps1               — Legacy PowerShell build script (still works)
│   ├── build_cyd.ps1           — CYD-specific build script
│   ├── build_cyd_boot.ps1      — PURR Kernel build script
│   ├── build_jc3248w535.ps1    — JC3248W535 build script (WIP)
│   ├── build_waveshare169.ps1  — Waveshare 1.69" build script (WIP)
│   └── targets/                — Per-target sdkconfig.defaults
│       ├── cyd.defaults        — ESP32, 4MB, ILI9341 (shared by cyd + cyd_boot)
│       ├── heltec.defaults     — ESP32-S3, 8MB, SSD1306
│       ├── jc3248w535.defaults — ESP32-S3, 16MB, 8MB PSRAM, ST7796 (WIP)
│       └── waveshare169.defaults — ESP32-S3, 4MB, ST7789 (WIP)
│
├── CoreOS/                     — ESP-IDF project root
│   ├── CMakeLists.txt          — project(purr_os_core), PROJECT_VER set here
│   ├── partitions_cyd.csv      — CYD 4MB layout
│   ├── partitions_heltec.csv   — Heltec layout
│   ├── partitions_jc3248w535.csv — JC3248W535 16MB layout
│   ├── partitions_waveshare169.csv — Waveshare 1.69" 4MB layout
│   │
│   ├── main/                   — Top-level IDF component
│   │   └── CMakeLists.txt      — Source selection, PURR_SHELL, PURR_DEFS, per-target blocks
│   │
│   ├── system/
│   │   ├── kernel/             — KITT kernel core
│   │   │   ├── purr_version.h  — Single source of truth: PURR_OS_VERSION, KITT_VERSION
│   │   │   ├── kitt.h/.cpp     — Kernel: boot, lifecycle, 60+ APIs
│   │   │   ├── main.cpp        — Arduino entry: kitt.init() + system_start()
│   │   │   ├── device_config.h — Parses device.json into device_config_t
│   │   │   ├── devices/        — Per-hardware JSON profiles
│   │   │   │   ├── cyd.json, heltec.json, jc3248w535.json, waveshare169.json, …
│   │   │   └── modules/
│   │   │       ├── display_ili9341.h/.cpp    — ILI9341 (CYD)
│   │   │       ├── display_ssd1306.h/.cpp    — SSD1306 OLED (Heltec)
│   │   │       ├── display_st7796.h/.cpp     — ST7796 480×320 (JC3248W535, WIP)
│   │   │       ├── display_st7789.h/.cpp     — ST7789 240×280 (Waveshare, WIP)
│   │   │       ├── touch_cst816s.h/.cpp      — CST816S I2C cap touch
│   │   │       ├── touch_gt911.h/.cpp        — GT911 I2C 5-point cap touch (WIP)
│   │   │       ├── partition_manager.h/.cpp  — OTA slot manager + SD dump/install
│   │   │       ├── purr_bootloader.h/.cpp    — Factory kernel UI (boot/wipe/install/SOS)
│   │   │       ├── stub_managers.cpp         — Linker stubs for factory kernel build
│   │   │       ├── wifi_manager.h/.cpp       — WiFi: scan, connect, NVS credentials
│   │   │       ├── bt_manager.h/.cpp         — Bluetooth BLE + Classic
│   │   │       ├── lora_manager.h/.cpp       — LoRa radio
│   │   │       ├── power_manager.h/.cpp      — Battery ADC, CPU freq scaling
│   │   │       ├── blackberry_ui.h/.cpp      — BB-style shell (MiniWin root window)
│   │   │       ├── explorer.h/.cpp           — Windows CE shell (MiniWin)
│   │   │       ├── mtp_manager.h/.cpp        — MTP USB file transfer
│   │   │       └── flasher.h/.cpp            — OTA partition flasher module
│   │   │
│   │   ├── system/             — System task
│   │   │   └── main.cpp        — Boot decision: chainload / SOS / bootloader UI / shell
│   │   │
│   │   └── micropython/        — MicroPython runtime
│   │       ├── mpython_runtime.h/.cpp
│   │       └── kitt_module.c   — `import kitt` Python extension
│   │
│   ├── apps/
│   │   └── smol/               — Smol OLED shell (Heltec / T-Deck)
│   │
│   └── components/
│       ├── TFT_eSPI/           — TFT_eSPI display library (IDF wrapper)
│       └── miniwinwm/          — MiniWin WM + PURR_CYD HAL port
│           └── MiniWin/hal/PURR_CYD/
│               ├── miniwin_config.h  — 320×240 landscape, 20ms tick
│               ├── hal_lcd.cpp       — Draws via display_ili9341
│               ├── hal_touch.cpp     — Reads via touch_cst816s
│               ├── hal_timer.cpp     — esp_timer 20ms tick
│               ├── hal_delay.cpp     — vTaskDelay / ets_delay_us
│               └── hal_non_vol.cpp   — NVS calibration storage
│
├── LoRa Kernels/               — Swappable LoRa backends
│   ├── SX1262/                 — Heltec V3, T-Deck (default)
│   ├── RAK3172/                — UART AT — CattoBoardV1 PCB
│   └── SX1276_RFM95W/          — Generic RFM95W breakout
│
└── PURR_OS_docs/               — Hardware docs, schematics
```

---

## Requirements

> **ESP-IDF v5.3.x is required — no other version is supported.**

PURR OS targets **ESP-IDF 5.3.x** (tested on **v5.3.5**). Do not use v5.4+ or v5.2 — the arduino-esp32 3.1.x managed component pins to `>=5.3,<5.4`, and the build system applies patches specific to IDF 5.3.x internals (`esp_driver_gpio`, `esp_timer` component split).

Install: [ESP-IDF v5.3.5 Getting Started](https://docs.espressif.com/projects/esp-idf/en/v5.3.5/esp32/get-started/)

On Windows the recommended path is `C:\esp\v5.3.5\esp-idf` with IDF Tools installed via the ESP-IDF installer. The SDK scripts auto-detect IDF via `IDF_PATH`.

---

## First-Time Setup

1. Install **ESP-IDF v5.3.5**
2. Clone this repo — MiniWin source must be present in `CoreOS/components/miniwinwm/MiniWin/` (a FATAL_ERROR will tell you if it is missing at cmake time)
3. Run `.\Builder\SDK.ps1` — sources IDF, walks through target/module/shell selection, applies arduino-esp32 patches, builds and flashes

For CYD first flash (two images required):
```powershell
.\Builder\build_cyd.ps1 -FullBuild -Clean -FullFlash COM8
```
This builds the PURR Kernel and userland back-to-back and flashes everything in one esptool pass.

---

## Contributing

This project is open-source and very much needs humans. Current open areas:

| Area | Status | Notes |
|------|--------|-------|
| Explorer shell | 🚧 Stub | Windows CE / PDA UI on MiniWin |
| JC3248W535 port | 🚧 WIP | Compiles, pins unverified on hardware |
| Waveshare 1.69" port | 🚧 WIP | Compiles, pins unverified on hardware |
| T-Deck port | 🚧 WIP | ST7789 driver + BB6 keyboard shell |
| App SDK | 📝 Planned | `.meow` MicroPython API docs + examples |
| Kernel OTA | 💡 Idea | Safe factory partition update mechanism |
| Testing | 🙏 Always needed | Hardware verification, flash compatibility |

Issues and PRs welcome.
