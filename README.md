# PURR OS — v0.6.0

> **MEGA DISCLAIMER:** This project is very much vibe-coded. It has gone from 0 to "Jesus Christ" at an alarming rate. It is fully open-source and humans are actively encouraged to help. Please.

**P.U.R.R.** = Portable Unified Runtime & Radio Operating System
Powered by the **K.I.T.T** (Kernel Interface Translation Toolkit) kernel — a modular, MicroPython-backed embedded OS for ESP32 hardware.

---

## Versions

| Component | Version |
|-----------|---------|
| PURR OS   | v0.6.0  |
| KITT      | v0.3.0  |

---

## What is this?

PURR OS is an embedded operating system for ESP32 devices. It runs a custom kernel (KITT), exposes a MicroPython runtime for `.meow` apps, manages optional radio/BT/USB modules, and renders a full windowed UI via the **MiniWin** window manager. Think Windows CE vibes, on a $10 ESP32 LCD board.

---

## Supported Targets

| Target | Chip | Display | Touch | Notes |
|--------|------|---------|-------|-------|
| `heltec` | ESP32-S3 | SSD1306 OLED | — | WiFi + LoRa, Smol shell |
| `cyd` | ESP32-2432S024C | ILI9341 2.4" | CST816S cap | Full OS image → `ota_0` |
| `cyd_boot` | ESP32-2432S024C | ILI9341 2.4" | CST816S cap | Minimal factory bootloader → `factory` |
| `tdeck` | ESP32-S3 | ST7789 | trackball | WIP |

---

## Partition Layout (CYD)

```
nvs       20 KB   — NVS key-value store
otadata    8 KB   — OTA boot selection
factory    1 MB   — cyd_boot: recovery bootloader, never overwritten by OTA
ota_0      1.5 MB — cyd: full PURR OS image, updated via OTA
ota_1      1 MB   — custom slot (flash any third-party firmware)
spiffs   448 KB   — filesystem: device config, app data, logs
```

Hold **GPIO0** while running the full OS to reboot to the factory bootloader. The bootloader scans all OTA slots, reads each image's firmware version from flash, and lets you **Boot**, **Wipe**, or **Install** per slot.

---

## UI Shells (CYD — powered by MiniWin)

All CYD UI shells run on top of **[MiniWin](https://github.com/miniwinwm/miniwinwm)** (MIT licensed), wired to the ILI9341 and CST816S via a PURR-specific HAL port (`CoreOS/components/miniwinwm/MiniWin/hal/PURR_CYD/`).

| Shell | Description |
|-------|-------------|
| `explorer` | Windows CE / PDA style — taskbar, overlapping windows, Start launcher |
| `blackberry` | BlackBerry-style fixed layout — status bar, app drawer, BB6 nav |
| `both` | Explorer preferred, BlackberryUI as fallback (default) |
| `smol` | Minimal OLED shell (Heltec / T-Deck) |
| `none` | Headless — no UI shell compiled |

---

## Kernel Modules

| Module | CMake Flag | Default | Notes |
|--------|-----------|---------|-------|
| Bluetooth | `PURR_ENABLE_BT` | ON | BLE + Classic |
| LoRa Radio | `PURR_ENABLE_LORA` | ON (heltec/tdeck) | SX1262 / RAK3172 / SX1276 |
| Meshtastic | `PURR_ENABLE_MESH` | OFF | Requires LoRa |
| MTP USB | `PURR_ENABLE_MTP` | OFF | USB file transfer |
| OTA Flasher | `PURR_ENABLE_FLASHER` | OFF | Partition flasher module |
| MicroPython | `BUILD_MINI=0` | ON | `.meow` app runtime |

---

## Build Tools

### SDK (recommended)

```powershell
# Interactive wizard — picks target, shell, modules, ports
.\Builder\SDK.ps1

# Direct build + flash
.\Builder\SDK.ps1 -Target cyd -Shell explorer -Build -Flash COM8
.\Builder\SDK.ps1 -Target cyd_boot -Build -Flash COM8 -Clean

# Clean build, no Bluetooth
.\Builder\SDK.ps1 -Target heltec -Build -Clean -NoBt
```

`SDK.ps1` sources IDF automatically and delegates to `sdk_core.py`. Config is saved to `Builder/purr_sdk.cfg` between runs.

### First flash (CYD — two images required)

```powershell
# 1. Factory bootloader — flash once, lives permanently at 0x10000
.\Builder\SDK.ps1 -Target cyd_boot -Build -Flash COM8

# 2. Full OS — flashes to ota_0 at 0x110000, OTA-updatable
.\Builder\SDK.ps1 -Target cyd -Build -Flash COM8
```

### Legacy build script

```powershell
.\Builder\Build.ps1                          # interactive wizard
.\Builder\Build.ps1 -Target heltec -Flash COM5
```

---

## Project Structure

```
PURR-OS/
│
├── Builder/                    — Build tooling (run from here)
│   ├── SDK.ps1                 — Recommended entry point: sources IDF, calls sdk_core.py
│   ├── sdk_core.py             — Python SDK core: interactive wizard, build/flash/monitor
│   ├── Build.ps1               — Legacy PowerShell build script (still works)
│   ├── purr_sdk.cfg            — Saved SDK config (gitignored, generated on first run)
│   └── targets/                — Per-target sdkconfig.defaults files
│       ├── heltec.defaults     — ESP32-S3, 8MB flash, SSD1306
│       └── cyd.defaults        — ESP32, 4MB flash, ILI9341 (shared by cyd + cyd_boot)
│
├── CoreOS/                     — ESP-IDF project root (build happens here)
│   ├── CMakeLists.txt          — IDF project entry (minimal, delegates to main/)
│   ├── idf_component.yml       — Managed component dependencies
│   ├── partitions_cyd.csv      — CYD partition table (factory + ota_0 + ota_1 + spiffs)
│   ├── partitions_heltec.csv   — Heltec partition table
│   │
│   ├── main/                   — Top-level IDF component (all build logic lives here)
│   │   └── CMakeLists.txt      — Source selection, module flags, PURR_DEFS, PURR_SHELL
│   │
│   ├── system/
│   │   ├── kernel/             — KITT kernel core
│   │   │   ├── kitt.h/.cpp     — Main kernel: boot sequence, lifecycle, all 60+ APIs
│   │   │   ├── main.cpp        — Arduino entry point (calls kitt.init + system_start)
│   │   │   ├── device_config.h — Parses device.json into device_config_t
│   │   │   ├── idf_compat.c    — IDF 5.x compat shims
│   │   │   ├── devices/        — Per-hardware JSON profiles (cyd.json, heltec.json, etc.)
│   │   │   └── modules/        — Optional kernel modules (compiled per flags)
│   │   │       ├── display_ili9341.h/.cpp    — ILI9341 display driver (CYD)
│   │   │       ├── display_ssd1306.h/.cpp    — SSD1306 OLED driver (Heltec)
│   │   │       ├── touch_cst816s.h/.cpp      — CST816S capacitive touch (CYD S024C)
│   │   │       ├── partition_manager.h/.cpp  — OTA slot manager, pm_boot_slot/factory
│   │   │       ├── purr_bootloader.h/.cpp    — Factory image: generic slot scanner UI
│   │   │       ├── wifi_manager.h/.cpp       — WiFi: scan, connect, NVS credentials
│   │   │       ├── bt_manager.h/.cpp         — Bluetooth BLE + Classic
│   │   │       ├── lora_manager.h/.cpp       — LoRa radio (swapped by LoRa Kernels/)
│   │   │       ├── power_manager.h/.cpp      — Battery ADC, CPU freq scaling
│   │   │       ├── blackberry_ui.h/.cpp      — BB-style shell (MiniWin)
│   │   │       ├── explorer.h/.cpp           — Windows CE/PDA shell (MiniWin)
│   │   │       ├── mtp_manager.h/.cpp        — MTP USB file transfer
│   │   │       └── flasher.h/.cpp            — OTA partition flasher
│   │   │
│   │   ├── bridge/             — Input/key translation layer
│   │   │   ├── main.cpp        — JSON keymap loader, GPIO→generic keycode
│   │   │   └── keymaps/        — Per-device keymap JSON files
│   │   │
│   │   ├── system/             — System task (boots correct shell based on image type)
│   │   │   └── main.cpp        — PURR_IS_BOOTLOADER_IMG → bootloader; else → UI shell
│   │   │
│   │   └── micropython/        — MicroPython runtime
│   │       ├── mpython_runtime.h/.cpp  — Process table, exec_app, KITT bridge
│   │       └── kitt_module.c           — `import kitt` Python extension module
│   │
│   ├── apps/
│   │   └── smol/               — Smol OLED shell (Heltec / T-Deck)
│   │       └── smol.h/.cpp     — 8-row text shell, app list, PURR menu
│   │
│   └── components/             — Local IDF components (third-party, version-locked)
│       ├── TFT_eSPI/           — TFT_eSPI display library (ILI9341 driver backend)
│       │   └── CMakeLists.txt  — IDF wrapper (cyd-only, registers single .cpp)
│       └── miniwinwm/          — MiniWin window manager (fork of miniwinwm/miniwinwm)
│           ├── CMakeLists.txt  — IDF wrapper: globs core sources, adds PURR_CYD HAL
│           └── MiniWin/
│               ├── miniwin.c/h         — Core WM engine
│               ├── dialogs/ ui/ gl/    — Widgets, dialogs, graphics primitives
│               └── hal/
│                   ├── hal_lcd.h       — LCD HAL interface (implemented by PURR_CYD)
│                   ├── hal_touch.h     — Touch HAL interface
│                   ├── hal_timer.h     — Timer HAL interface
│                   ├── hal_delay.h     — Delay HAL interface
│                   ├── hal_non_vol.h   — NVS HAL interface
│                   ├── PURR_CYD/       — PURR OS HAL port (ILI9341 + CST816S + NVS)
│                   │   ├── miniwin_config.h  — 320×240 landscape config
│                   │   ├── hal_lcd.cpp       — Draws via display_ili9341
│                   │   ├── hal_touch.cpp     — Reads via touch_cst816s
│                   │   ├── hal_timer.cpp     — esp_timer 20ms tick
│                   │   ├── hal_delay.cpp     — Arduino delay/delayMicroseconds
│                   │   └── hal_non_vol.cpp   — NVS calibration storage
│                   └── [DevKitC/ linux/ stm32/ ...]  — Upstream platform ports (unused)
│
├── LoRa Kernels/               — Swappable LoRa radio backends (drop into modules/)
│   ├── SX1262/                 — SPI — Heltec V3, T-Deck (default)
│   ├── RAK3172/                — UART AT — CattoBoardV1 PCB
│   └── SX1276_RFM95W/          — SPI — generic RFM95W breakout
│
└── PURR_OS_docs/               — Hardware docs and schematics reference
    └── board.md                — Token-efficient KiCad schematic summary
```

---

## Requirements

> **ESP-IDF v5.3.x is required — no other version is supported.**

PURR OS targets **ESP-IDF 5.3.x** (tested on **v5.3.5**). Do not use v5.4+ or v5.2 — the arduino-esp32 3.1.x managed component pins to `>=5.3,<5.4`, and the build system applies patches specific to IDF 5.3.x internals (`esp_driver_gpio`, `esp_timer` component split).

Install the correct version: [ESP-IDF v5.3.5 Getting Started](https://docs.espressif.com/projects/esp-idf/en/v5.3.5/esp32/get-started/)

On Windows the recommended path is `C:\esp\v5.3.5\esp-idf` with the IDF Tools installed via the ESP-IDF installer. The SDK scripts detect IDF automatically via the `IDF_PATH` environment variable.

---

## First-Time Setup

1. Install **ESP-IDF v5.3.5** (see Requirements above)
2. Clone this repo (MiniWin is already included in `CoreOS/components/miniwinwm/`)
3. Run `.\Builder\SDK.ps1` — it sources IDF, walks you through target/module/shell selection, applies arduino-esp32 patches automatically, then builds and flashes

---

## Contributing

This project is open-source and very much needs humans. If you want to help:
- **Explorer shell** — Windows CE / PDA UI implementation on MiniWin
- **BlackberryUI** — BB6-style chrome rebuilt on MiniWin primitives
- **App SDK** — `.meow` MicroPython app API documentation and examples
- **T-Deck port** — ST7789 driver, BB6 keyboard shell, trackball input
- **Testing** — hardware verification, build system issues, flash compatibility

Issues and PRs welcome.
