# PURR OS — v0.9.0

> **MEGA DISCLAIMER:** This project is very much vibe-coded. It has gone from 0 to "Jesus Christ" at an alarming rate. It is fully open-source and humans are actively encouraged to help. Please.

**P.U.R.R.** = Portable Unified Runtime & Radio Operating System
Powered by the **K.I.T.T** (Kernel Interface Translation Toolkit) kernel — a modular C++ embedded OS for ESP32 hardware, built on pure ESP-IDF 5.3.5.

---

## Using Docs with AI Models

All PURR OS documentation is version-controlled and designed for AI-assisted development. When adapting or extending the codebase with Claude, GPT, or similar models:

1. **Reference the docs** — Read `docs/PURR_OS_docs/` files relevant to your task
2. **Ask specific questions** — "How do I add a new sensor module?" (see `02_KITT_Kernel_Spec.md`)
3. **Get accurate context** — AI models produce better code when given full specification

**[-> See AI Development Guide ->](docs/PURR_OS_docs/00_AI_Development_Guide.md)**

---

## Quick Start

### Windows
```powershell
.\Builder\SDK.ps1 -Target cyd_s028r -Build
.\Builder\SDK.ps1 -Target cyd_s028r -Flash COM8
```

### Linux / macOS
```bash
chmod +x setup_linux.sh
./setup_linux.sh              # One-time setup (installs ESP-IDF 5.3.5)
./Builder/sdk.sh --target cyd_s028r --build
./Builder/sdk.sh --target cyd_s028r --flash /dev/ttyUSB0
```

For more details, see **[QUICKSTART.md](QUICKSTART.md)**.

---

## Versions

| Component | Version | Release Date |
|-----------|---------|--------------|
| PURR OS   | v0.9.0  | 2026-06-07   |
| KITT      | v0.5.0  | 2026-06-07   |

Version strings are defined in [CoreOS/system/kernel/purr_version.h](CoreOS/system/kernel/purr_version.h) and automatically embedded into the firmware image via `esp_app_desc_t` — visible in the bootloader's slot card and on the homescreen.

Full release history is in **[CHANGELOG.md](CHANGELOG.md)** at the repository root.

### Release Notes: v0.9.0 / KITT v0.5.0

Major updates:
- Arduino dependency removed entirely — pure ESP-IDF 5.3.5 across all drivers
- All hardware drivers reorganized as discrete IDF components under `CoreOS/components/drv_*/`
- TFT_eSPI is self-contained in `lib_tftespi/` with a minimal Arduino shim; no `lib_arduino` needed
- WiFi stub for heltec — `drv_wifi` compiles a no-op stub so heltec links without the WiFi stack
- `PURR_HAS_MINIWIN` guard in `system/system/main.cpp` — `cyd_boot` no longer requires the MiniWin header
- SD card support live in the factory partition — install, backup, restore firmware from SD with no network
- IDF include propagation workarounds applied consistently (`idf_component_get_property` + `target_link_libraries`)
- All three targets (cyd_s024c, cyd_boot, heltec) build clean

---

## What is this?

PURR OS is an embedded operating system for ESP32 devices. It runs a custom kernel (KITT), exposes a MicroPython runtime for `.meow` apps, manages optional radio/BT/USB modules, and renders a full windowed UI via the **MiniWin** window manager. Think Windows CE vibes, on a $10 ESP32 LCD board.

The architecture splits across two flash partitions: a small **PURR Kernel** that lives in the factory slot (OTA-immune, handles boot decisions and SD recovery) and the **PURR Userland** in ota_0 that gets updated over-the-air.

---

## Supported Targets

| Target | Chip | Display | Touch | Status | Notes |
|--------|------|---------|-------|--------|-------|
| `cyd_s028r` | ESP32-2432S028R | ILI9341 2.4" 320x240 | XPT2046 SPI | Active | Original variant |
| `cyd_s024c` | ESP32-2432S024C | ILI9341 2.4" 320x240 | CST816S I2C | Active | Newer variant |
| `cyd_boot` | ESP32-2432S024C | ILI9341 2.4" 320x240 | CST816S I2C | Active | PURR Kernel -> factory partition |
| `heltec` | ESP32-S3 | SSD1306 OLED 128x64 | — | Working | WiFi + LoRa, Smol shell |
| `tdeck` | ESP32-S3 | ST7789 | trackball | WIP | Shell pending |
| `jc3248w535` | ESP32-S3 | ST7796 3.5" 480x320 | GT911 cap | WIP | Verify pins before flashing |
| `waveshare169` | ESP32-S3 | ST7789 1.69" 240x280 | CST816S cap | WIP | Verify pins before flashing |

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

1. IDF bootloader reads OTA data -> jumps to factory (PURR Kernel)
2. PURR Kernel reads `esp_app_desc_t` from ota_0 — if it is a PURR image, chainloads in ~20ms
3. If **GPIO 0 is held** at power-on -> forces bootloader UI
4. If ota_0 has crashed 3 times in a row without clearing the counter -> **SOS mode**
5. If ota_0 is empty or non-PURR firmware -> bootloader UI (pure passthrough)

The PURR Kernel is **OTA-immune** — it can only be updated by flashing over USB. This means the device always has a recovery path regardless of what is in the OTA slots.

---

## PURR Kernel (factory partition)

The factory image is a full recovery environment (394 KB, fits comfortably in the 1 MB factory slot):

- **Fast-path chainload** — boots PURR userland in ~20ms on every normal power-on
- **Crash-loop detection** — KITT increments an NVS counter before each chainload and the userland clears it on successful init; 3 failures -> SOS mode
- **SOS recovery screen** — wipe slot / boot anyway / dismiss
- **Bootloader UI** — lists all OTA slots with firmware version read direct from flash; per-slot Boot / Wipe / Install
- **SD card install** — flashes any `.bin` from SD card into any OTA slot using the IDF OTA API (CS GPIO5, MOSI 23, MISO 19, SCLK 18)
- **Firmware backup** — before overwriting a slot that has PURR firmware, offers to dump it to SD; after install, offers to restore or boot new firmware
- **Third-party firmware** — ota_1 slot can hold any ESP32 firmware; PURR Kernel acts as a pure passthrough bootloader for non-PURR images
- **No network required** — factory partition has no WiFi stack

---

## UI Shells (CYD — powered by MiniWin)

All CYD UI shells run on top of **[MiniWin](https://github.com/miniwinwm/miniwinwm)** (MIT licensed), wired to the ILI9341 and CST816S via a PURR-specific HAL port (`CoreOS/components/lib_miniwin/MiniWin/hal/PURR_CYD/`).

| Shell | Flag | Description |
|-------|------|-------------|
| `blackberry` | `PURR_HAS_BLACKBERRY_UI` | BlackBerry OS 6-style — status bar, swipe-up app drawer, BB nav |
| `explorer` | `PURR_HAS_EXPLORER` | Windows CE / PDA style — taskbar, overlapping windows, Start launcher |
| `smol` | — | Minimal OLED shell (Heltec / T-Deck) |

MiniWin runs as a FreeRTOS task (core 1, priority 3, 12KB stack). Apps open as borderless `mw_add_window()` windows on top.

> MiniWin source must be cloned into `CoreOS/components/lib_miniwin/` before building. A FATAL_ERROR is raised at cmake time if it is missing.

---

## Kernel Modules

| Module | CMake Flag | Default | Notes |
|--------|-----------|---------|-------|
| Bluetooth | `PURR_ENABLE_BT` | ON | BLE + Classic |
| LoRa Radio | `PURR_ENABLE_LORA` | ON (heltec/tdeck) | SX1262 / RAK3172 / SX1276 |
| Meshtastic | `PURR_ENABLE_MESH` | OFF | Requires LoRa |
| MTP USB | `PURR_ENABLE_MTP` | OFF | USB file transfer |
| OTA Flasher | `PURR_ENABLE_FLASHER` | OFF | In-app OTA flasher module |
| MicroPython | `BUILD_MINI=0` | ON (off for cyd_boot always) | `.meow` app runtime |

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
├── CHANGELOG.md                — Full release history (all versions)
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
│   ├── partitions_jc3248w535.csv
│   ├── partitions_waveshare169.csv
│   │
│   ├── main/                   — Top-level IDF component
│   │   └── CMakeLists.txt      — Source selection, PURR_SHELL, PURR_DEFS, per-target blocks
│   │
│   ├── system/
│   │   ├── kernel/             — KITT kernel core
│   │   │   ├── purr_version.h  — Single source of truth: PURR_OS_VERSION, KITT_VERSION
│   │   │   ├── kitt.h/.cpp     — Kernel: boot, lifecycle, 60+ APIs
│   │   │   ├── main.cpp        — IDF app_main entry: kitt.init() + system_start()
│   │   │   ├── device_config.h — Parses device.json into device_config_t
│   │   │   ├── devices/        — Per-hardware JSON profiles
│   │   │   └── modules/        — Kernel modules (partition_manager, purr_bootloader, ...)
│   │   ├── system/
│   │   │   └── main.cpp        — system_task: chainload logic, shell launch
│   │   └── micropython/        — MicroPython runtime (BUILD_MINI=0 only)
│   │
│   └── components/             — IDF components (all hardware drivers)
│       ├── drv_display/        — ILI9341, ST7789, ST7796, SSD1306, ILI9488
│       ├── drv_touch/          — CST816S, XPT2046, GT911, MXT336T
│       ├── drv_bt/             — Bluetooth manager
│       ├── drv_gps/            — GPS UART manager
│       ├── drv_hid/            — USB HID keyboard matrix
│       ├── drv_lora/           — LoRa manager + swappable kernels (SX1262/SX1276/RAK3172)
│       ├── drv_wifi/           — WiFi manager (no-op stub for heltec)
│       ├── lib_tftespi/        — TFT_eSPI with self-contained minimal Arduino shim
│       ├── lib_miniwin/        — MiniWin WM + PURR_CYD HAL (clone separately)
│       └── lib_radiolib/       — RadioLib LoRa physical layer
│
└── docs/
    ├── PURR_OS_docs/           — Architecture, kernel spec, UI specs, boot sequence
    └── CHANGELOG.md            — Redirects to root CHANGELOG.md
```

---

## Requirements

> **ESP-IDF v5.3.x is required — no other version is supported.**

PURR OS targets **ESP-IDF 5.3.x** (tested on **v5.3.5**). Do not use v5.4+ or v5.2 — the arduino-esp32 3.1.x managed component pins to `>=5.3,<5.4`, and the build system applies patches specific to IDF 5.3.x internals (`esp_driver_gpio`, `esp_timer` component split).

Install: ESP-IDF v5.3.5 Getting Started — https://docs.espressif.com/projects/esp-idf/en/v5.3.5/esp32/get-started/

On Windows the recommended path is `C:\esp\v5.3.5\esp-idf` with IDF Tools installed via the ESP-IDF installer. The SDK scripts auto-detect IDF via `IDF_PATH`.

---

## First-Time Setup

1. Install **ESP-IDF v5.3.5**
2. Clone this repo — MiniWin source must be present in `CoreOS/components/lib_miniwin/MiniWin/` (a FATAL_ERROR will tell you if it is missing at cmake time)
3. Run `.\Builder\SDK.ps1` — sources IDF, walks through target/module/shell selection, applies arduino-esp32 patches, builds and flashes

For CYD first flash (two images required):
```powershell
.\Builder\build_cyd.ps1 -FullBuild -Clean -FullFlash COM8
```
This builds the PURR Kernel and userland back-to-back and flashes everything in one esptool pass.

---

## Documentation

All PURR OS specifications and architecture docs are in **[`docs/PURR_OS_docs/`](docs/PURR_OS_docs/)**:

| Doc | Content |
|-----|---------|
| [`00_AI_Development_Guide.md`](docs/PURR_OS_docs/00_AI_Development_Guide.md) | How to use these docs with AI models |
| [`01_Architecture.md`](docs/PURR_OS_docs/01_Architecture.md) | System design, component relationships, layer separation |
| [`02_KITT_Kernel_Spec.md`](docs/PURR_OS_docs/02_KITT_Kernel_Spec.md) | Kernel API, task lifecycle, memory model |
| [`05_Boot_Sequence.md`](docs/PURR_OS_docs/05_Boot_Sequence.md) | Hardware init, OTA chainload, SOS recovery |
| [`06_WindowsCE_UI_Spec.md`](docs/PURR_OS_docs/06_WindowsCE_UI_Spec.md) | UI shell architecture, MiniWin integration |
| [`12_TDeck_BlackBerry6_UI_Spec.md`](docs/PURR_OS_docs/12_TDeck_BlackBerry6_UI_Spec.md) | BlackBerry OS 6-inspired shell implementation |
| [and more...](docs/PURR_OS_docs/) | App bundles, protocols, hardware specs |
| **[CHANGELOG.md](CHANGELOG.md)** | **Full release history — all versions** |

---

## Contributing

This project is open-source and very much needs humans. Current open areas:

| Area | Status | Notes |
|------|--------|-------|
| Explorer shell | Stub | Windows CE / PDA UI on MiniWin |
| JC3248W535 port | WIP | Compiles, pins unverified on hardware |
| Waveshare 1.69" port | WIP | Compiles, pins unverified on hardware |
| T-Deck port | WIP | ST7789 driver + BB6 keyboard shell |
| App SDK | Planned | `.meow` MicroPython API docs + examples |
| Kernel OTA | Idea | Safe factory partition update mechanism |
| Testing | Always needed | Hardware verification, flash compatibility |

Issues and PRs welcome.
