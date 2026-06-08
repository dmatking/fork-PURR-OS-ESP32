# PURR OS — v0.9.1

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
.\SDK\SDK.ps1 -Target cyd_s028r -Build
.\SDK\SDK.ps1 -Target cyd_s028r -Flash COM8
```

### Linux / macOS
```bash
./SDK/setup_linux.sh          # One-time setup (installs ESP-IDF 5.3.5)
./SDK/sdk.sh --target cyd_s028r --build
./SDK/sdk.sh --target cyd_s028r --flash /dev/ttyUSB0
```

For more details, see **[docs/QUICKSTART.md](docs/QUICKSTART.md)**.

---

## Versions

| Component | Version | Release Date |
|-----------|---------|--------------|
| PURR OS   | v0.9.1  | 2026-06-07   |
| KITT      | v0.5.1  | 2026-06-07   |

Version strings are defined in [CoreOS/system/kernel/purr_version.h](CoreOS/system/kernel/purr_version.h) and automatically embedded into the firmware image via `esp_app_desc_t` — visible in the bootloader's slot card and on the homescreen.

Full release history is in **[CHANGELOG.md](CHANGELOG.md)** at the repository root.

### Release Notes: v0.9.1 / KITT v0.5.1

- **Arduino-ESP32 restored:** `lib_arduino` is now a passthrough to `espressif__arduino-esp32` managed component; hand-written shim deleted. Required by TFT_eSPI, RadioLib, LoRa, USB HID, and MiniWin display drivers
- `purr_idf_compat.h` simplified to `#include <Arduino.h>` — all existing include sites unchanged
- MiniWin HAL touch fix: `mw_hal_touch_get_point()` now polls fresh CST816S data — touch was silently ignored in all previous builds
- Windows CE shell: single full-screen window eliminates focus/z-order issues
- Start button renders sunken while menu is open; raised otherwise
- Start menu items are now interactive: press highlight (60ms), "About" dialog, "Shut Down" reboots
- `mw_paint_all()` on init clears calibration screen artifact

### Release Notes: v0.9.0 / KITT v0.5.0

- Arduino dependency removed entirely — pure ESP-IDF 5.3.5 across all drivers
- All hardware drivers reorganized as discrete IDF components under `CoreOS/components/drv_*/`
- TFT_eSPI self-contained in `lib_tftespi/` with a minimal shim — no `lib_arduino` dep
- WiFi stub for heltec — `drv_wifi` compiles a no-op stub so heltec links without the WiFi stack
- `PURR_HAS_MINIWIN` guard in `system/system/main.cpp` for `cyd_boot`
- SD card support live in the factory partition — install, backup, restore firmware from SD, no network required
- IDF include propagation workarounds applied consistently (`idf_component_get_property` + `target_link_libraries`)
- All three targets (cyd_s024c, cyd_boot, heltec) build clean

---

## What is this?

PURR OS is an embedded operating system for ESP32 devices. It runs a custom kernel (KITT), exposes a MicroPython runtime for `.meow` apps, manages optional radio/BT/USB modules, and renders a full windowed UI via the **MiniWin** window manager. Think Windows CE vibes, on a $10 ESP32 LCD board.

The architecture splits across two flash partitions: a **PURR Kernel** in the factory slot (OTA-immune, handles boot decisions and SD recovery) and the **PURR Userland** in ota_0 that gets updated over-the-air.

---

## Supported Targets

| Target | Chip | Display | Touch | Status | Notes |
|--------|------|---------|-------|--------|-------|
| `cyd_s028r` | ESP32-2432S028R | ILI9341 2.4" 320x240 | XPT2046 SPI | Active | Original CYD variant |
| `cyd_s024c` | ESP32-2432S024C | ILI9341 2.4" 320x240 | CST816S I2C | Active | Newer CYD variant |
| `cyd_boot` | ESP32-2432S024C | ILI9341 2.4" 320x240 | CST816S I2C | Active | PURR Kernel — flashed to factory partition |
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

The factory image is a full recovery environment (394 KB — fits in the 1 MB factory slot with 62% free):

- **Fast-path chainload** — boots PURR userland in ~20ms on every normal power-on
- **Crash-loop detection** — KITT increments an NVS counter before each chainload; userland clears it on successful init; 3 failures -> SOS mode
- **SOS recovery screen** — wipe slot / boot anyway / dismiss
- **Bootloader UI** — lists all OTA slots with firmware version read direct from flash; per-slot Boot / Wipe / Install
- **SD card install** — flashes any `.bin` from SD card into any OTA slot (CS GPIO5, MOSI 23, MISO 19, SCLK 18)
- **Firmware backup** — before overwriting a PURR slot, offers to dump it to SD; after install, offers to restore or boot new firmware
- **No network required** — factory partition has no WiFi stack

---

## UI Shells

### MiniWin shells (CYD)

CYD shells run on **[MiniWin](https://github.com/miniwinwm/miniwinwm)** (MIT licensed), wired to the display and touch via the `Shells/purr_wm/` IDF component (MiniWin HAL adapter + window manager API).

| Shell | Flag | Description |
|-------|------|-------------|
| `blackberry` | `PURR_HAS_BLACKBERRY_UI` | BlackBerry OS 6-style — status bar, swipe-up app drawer |
| `explorer` | `PURR_HAS_EXPLORER` | Windows CE / PDA style — taskbar, overlapping windows |

MiniWin runs as a FreeRTOS task (core 1, priority 3, 12KB stack). Shell implementations live in `WIP/` while under development; active ones are pulled into `CoreOS/system/kernel/modules/` once stable.

### Smol (Heltec / T-Deck)
Minimal OLED text shell running directly against `display_ssd1306` — 8-row layout, UP/DOWN/SELECT navigation, app launcher.

> MiniWin source is in `CoreOS/components/lib_miniwin/MiniWin/`. A FATAL_ERROR is raised at cmake time if it is missing.

---

## Kernel Modules

| Module | CMake Flag | Notes |
|--------|-----------|-------|
| Bluetooth | `PURR_ENABLE_BT` | BLE + Classic; driver in `drv_bt/` |
| LoRa Radio | `PURR_ENABLE_LORA` | SX1262 / RAK3172 / SX1276; driver in `drv_lora/` |
| Meshtastic mesh | `PURR_ENABLE_MESH` | Requires LoRa; `purr_mesh.cpp` + `lib_mesh_pb/` + `lib_nanopb/` |
| LTE | `PURR_ENABLE_LTE` | Cellular modem; driver in `drv_lte/` |
| GPS | `PURR_ENABLE_GPS` | UART GPS; driver in `drv_gps/` |
| MTP USB | `PURR_ENABLE_MTP` | USB file transfer |
| OTA Flasher | `PURR_ENABLE_FLASHER` | In-app OTA flasher module |
| Lua runtime | `PURR_ENABLE_LUA` | `lua_runtime.cpp` + `lib_lua/` |
| MicroPython | `BUILD_MINI=0` | `.meow` app runtime; always off for `cyd_boot` |

---

## Build Tools

Build scripts live in `SDK/`. There is no `Builder/` directory.

### SDK (recommended)

```powershell
# Interactive menu — target/shell/module/port selection persisted between runs
.\SDK\SDK.ps1

# Per-target scripts
.\SDK\build_cyd.ps1
.\SDK\build_cyd_boot.ps1
.\SDK\build_heltec.ps1
.\SDK\build_jc3248w535.ps1
.\SDK\build_waveshare169.ps1
.\SDK\build_tdeck.ps1

# Direct build + flash
.\SDK\SDK.ps1 -Target cyd -Shell blackberry -Build -Flash COM8
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
# Build both and flash everything in one go (recommended)
.\SDK\build_cyd.ps1 -FullBuild -Clean -FullFlash COM8

# Or manually, one image at a time:
.\SDK\build_cyd_boot.ps1 -Build -Flash COM8   # factory kernel (flash once)
.\SDK\build_cyd.ps1 -Build -Flash COM8         # userland (OTA-updatable)
```

---

## Project Structure

```
PURR-OS-ESP32/
│
├── CHANGELOG.md                — Full release history (all versions)
├── PURR_TODO.md                — Open tasks and known issues
│
├── SDK/                        — Build tooling (run from here)
│   ├── SDK.ps1                 — Main entry point: sources IDF, calls sdk_core.py
│   ├── sdk_core.py             — Python SDK: interactive menu, build/flash/monitor
│   ├── Build.ps1               — Legacy PowerShell build script
│   ├── build_cyd.ps1           — CYD userland build script
│   ├── build_cyd_boot.ps1      — PURR Kernel (factory) build script
│   ├── build_heltec.ps1
│   ├── build_jc3248w535.ps1
│   ├── build_waveshare169.ps1
│   ├── build_tdeck.ps1
│   ├── sdk.sh                  — Linux/macOS wrapper
│   ├── setup_linux.sh          — One-time ESP-IDF 5.3.5 install script
│   ├── _idf.ps1                — IDF environment sourcing helper
│   ├── HOWTO.md                — SDK usage guide
│   └── targets/                — Per-target sdkconfig.defaults
│       ├── cyd.defaults        — ESP32, 4MB flash, ILI9341
│       ├── heltec.defaults     — ESP32-S3, 8MB flash, SSD1306
│       ├── jc3248w535.defaults — ESP32-S3, 16MB flash, 8MB PSRAM
│       ├── tdeck.defaults
│       └── waveshare169.defaults
│
├── CoreOS/                     — ESP-IDF project root
│   ├── CMakeLists.txt          — project(purr_os_core), PROJECT_VER, EXTRA_COMPONENT_DIRS
│   ├── sdkconfig_cyd           — Per-target saved configs (generated by idf.py)
│   ├── sdkconfig_cyd_boot
│   ├── sdkconfig_cyd_s024c
│   ├── partitions_cyd.csv      — 4MB: factory 1MB / ota_0 1.5MB / ota_1 1MB / spiffs 448KB
│   ├── partitions_heltec.csv
│   ├── partitions_jc3248w535.csv
│   ├── partitions_waveshare169.csv
│   │
│   ├── main/                   — Top-level IDF component
│   │   └── CMakeLists.txt      — Per-target SRCS/REQUIRES/DEFS selection
│   │
│   ├── system/
│   │   ├── kernel/             — KITT kernel core
│   │   │   ├── purr_version.h  — PURR_OS_VERSION, KITT_VERSION (single source of truth)
│   │   │   ├── kitt.h/.cpp     — Kernel: boot, lifecycle, 60+ APIs
│   │   │   ├── main.cpp        — IDF app_main: kitt.init() + system_start()
│   │   │   ├── device_config.h/.cpp  — Parses device.json -> device_config_t
│   │   │   ├── purr_idf_compat.h    — millis/delay/GPIO shims (pure IDF)
│   │   │   ├── idf_compat.c         — IDF 5.x compatibility shims
│   │   │   ├── devices/             — Per-hardware JSON profiles
│   │   │   │   ├── cyd.json, heltec.json
│   │   │   │   ├── jc3248w535.json, waveshare169.json
│   │   │   │   ├── box3.json, cattopad.json, ingenico.json
│   │   │   └── modules/             — Kernel modules (not hardware drivers)
│   │   │       ├── partition_manager.h/.cpp   — OTA slot scan, SD install/backup/wipe
│   │   │       ├── partition_manager_stubs.cpp — No-op stubs (unused in v0.9.0)
│   │   │       ├── purr_bootloader.h/.cpp      — Factory recovery UI
│   │   │       ├── stub_managers.cpp           — WiFi/power linker stubs for cyd_boot
│   │   │       ├── ui_stubs.cpp                — UI linker stubs for headless builds
│   │   │       ├── power_manager.h/.cpp        — Battery ADC, CPU freq scaling
│   │   │       ├── flasher.h/.cpp              — OTA flasher module
│   │   │       ├── mtp_manager.h/.cpp          — USB MTP file transfer
│   │   │       ├── mesh_manager.h/.cpp         — Meshtastic integration
│   │   │       ├── purr_mesh.h/.cpp            — PURR mesh protocol
│   │   │       ├── pi_manager.h/.cpp           — Raspberry Pi bridge
│   │   │       └── lua_runtime.h/.cpp          — Lua scripting runtime
│   │   │
│   │   ├── bridge/             — GPIO -> generic keycode translator
│   │   │   └── keymaps/        — Key layout maps
│   │   │
│   │   ├── micropython/        — MicroPython runtime (BUILD_MINI=0 only)
│   │   │   ├── mpython_runtime.h/.cpp
│   │   │   ├── kitt_module.c          — `import kitt` C extension
│   │   │   └── mpconfigport.h
│   │   │
│   │   └── system/             — System task
│   │       └── main.cpp        — Boot decision: chainload / SOS / bootloader UI / shell launch
│   │
│   └── components/             — IDF components (hardware drivers + libraries)
│       ├── drv_display/        — ILI9341, ST7789, ST7796, SSD1306, ILI9488
│       ├── drv_touch/          — CST816S, XPT2046, GT911, MXT336T
│       ├── drv_bt/             — Bluetooth manager (BLE + Classic)
│       ├── drv_gps/            — GPS UART manager
│       ├── drv_hid/            — USB HID keyboard matrix (moved from CattoHID/)
│       ├── drv_lora/           — LoRa manager + swappable kernels
│       │   └── kernels/        — sx1262/, rak3172/, sx1276/
│       ├── drv_lte/            — LTE cellular modem driver
│       ├── drv_wifi/           — WiFi manager (no-op stub for heltec)
│       ├── lib_tftespi/        — TFT_eSPI + minimal self-contained Arduino shim
│       ├── lib_miniwin/        — MiniWin window manager source + PURR_CYD HAL
│       │   └── MiniWin/hal/PURR_CYD/  — Display + touch + timer HAL for CYD
│       ├── lib_radiolib/       — RadioLib LoRa physical layer
│       ├── lib_lua/            — Lua scripting runtime
│       ├── lib_nanopb/         — Nanopb Protocol Buffers (for mesh)
│       ├── lib_mesh_pb/        — Meshtastic protobuf definitions
│       └── lib_arduino/        — Arduino compatibility layer (present, not used as dep)
│
├── Shells/                     — Extra IDF components registered via EXTRA_COMPONENT_DIRS
│   └── purr_wm/                — MiniWin HAL adapter + PURR window manager API
│       ├── purr_wm.h/.cpp      — Window manager wrapper
│       └── minwin_hal_adapter.cpp
│
├── Userland/                   — MicroPython app bundles (.meow format)
│   ├── apps/
│   │   ├── ClassicMac.meow/    — Mac System 6-style shell
│   │   ├── explorer.meow/      — File explorer app
│   │   ├── explorer_lvgl.meow/ — LVGL explorer variant
│   │   ├── finder.meow/        — Mac Finder-style app
│   │   ├── purr_ui.meow/       — PURR homescreen UI
│   │   └── smol.meow/          — Smol OLED shell as a .meow app
│   └── lib/
│       └── colors.py           — Shared color constants
│
├── WIP/                        — Shell implementations under development
│   ├── blackberry/             — BlackBerry OS 6-style shell (MiniWin)
│   ├── classicmac/             — Classic Mac System shell (MiniWin)
│   ├── explorer/               — Windows CE explorer shell (MiniWin)
│   └── heltec_shell/           — Heltec OLED shell
│
├── CattoHID/                   — ESP32-S2 USB HID firmware stub
│   └── CMakeLists.txt          — Driver source moved to CoreOS/components/drv_hid/
│
├── archive/                    — Historical artifacts (not built)
│   ├── sim/                    — Windows MiniWin UI simulator (Win32 + CMake)
│   ├── LVGL/                   — LVGL experiment notes and test code
│   ├── firmware/               — MicroPython binary archive
│   ├── device/                 — Early MicroPython kernel
│   └── emulator.py             — Desktop emulator prototype
│
└── docs/
    ├── CHANGELOG.md            — Redirects to root CHANGELOG.md
    ├── QUICKSTART.md           — Getting started guide
    ├── BUILDLOG.md             — Build notes and known issues log
    ├── LINUX_BUILD.md          — Linux-specific build instructions
    ├── PURR_IDF_MIGRATION.md   — Arduino -> pure IDF migration notes
    ├── PURR_WIP_DEVICES.md     — WIP target hardware notes
    └── PURR_OS_docs/           — Full specification docs
        ├── 00_AI_Development_Guide.md
        ├── 01_Architecture.md
        ├── 02_KITT_Kernel_Spec.md
        ├── 03_ControlPanel_Spec.md
        ├── 04_AppBundle_Format.md
        ├── 05_Boot_Sequence.md
        ├── 06_WindowsCE_UI_Spec.md
        ├── 09_CattoBoardV1_Spec.md
        ├── 10_Handshake_Protocols.md
        ├── 11_PURR_HID_Edition.md
        └── 12_TDeck_BlackBerry6_UI_Spec.md
```

---

## Requirements

> **ESP-IDF v5.3.x is required — no other version is supported.**

PURR OS targets **ESP-IDF 5.3.x** (tested on **v5.3.5**). Do not use v5.4+ or v5.2 — the arduino-esp32 3.1.x managed component pins to `>=5.3,<5.4`, and the build system applies patches specific to IDF 5.3.x internals.

Install: ESP-IDF v5.3.5 Getting Started — https://docs.espressif.com/projects/esp-idf/en/v5.3.5/esp32/get-started/

On Windows the recommended path is `C:\esp\v5.3.5\esp-idf` with IDF Tools installed via the ESP-IDF installer. The SDK scripts auto-detect IDF via `IDF_PATH`.

---

## First-Time Setup

1. Install **ESP-IDF v5.3.5**
2. Clone this repo — MiniWin source must be present in `CoreOS/components/lib_miniwin/MiniWin/` (a FATAL_ERROR will tell you at cmake time if it is missing)
3. Run `.\SDK\SDK.ps1` — sources IDF, walks through target/module/shell selection, builds and flashes

For CYD first flash (two images required):
```powershell
.\SDK\build_cyd.ps1 -FullBuild -Clean -FullFlash COM8
```
This builds the PURR Kernel and userland back-to-back and flashes everything in one esptool pass.

---

## Documentation

| Doc | Content |
|-----|---------|
| [00_AI_Development_Guide.md](docs/PURR_OS_docs/00_AI_Development_Guide.md) | How to use these docs with AI models |
| [01_Architecture.md](docs/PURR_OS_docs/01_Architecture.md) | System design, component layout, layer separation |
| [02_KITT_Kernel_Spec.md](docs/PURR_OS_docs/02_KITT_Kernel_Spec.md) | Kernel API, task lifecycle, memory model |
| [04_AppBundle_Format.md](docs/PURR_OS_docs/04_AppBundle_Format.md) | .meow bundle format and MicroPython API |
| [05_Boot_Sequence.md](docs/PURR_OS_docs/05_Boot_Sequence.md) | Hardware init, OTA chainload, SOS recovery |
| [06_WindowsCE_UI_Spec.md](docs/PURR_OS_docs/06_WindowsCE_UI_Spec.md) | Explorer shell architecture, MiniWin integration |
| [12_TDeck_BlackBerry6_UI_Spec.md](docs/PURR_OS_docs/12_TDeck_BlackBerry6_UI_Spec.md) | BlackBerry OS 6-inspired shell implementation |
| [CHANGELOG.md](CHANGELOG.md) | Full release history — all versions |
| [docs/QUICKSTART.md](docs/QUICKSTART.md) | Getting started, first flash walkthrough |
| [docs/BUILDLOG.md](docs/BUILDLOG.md) | Build notes, known issues |

---

## Contributing

This project is open-source and very much needs humans. Current open areas:

| Area | Status | Notes |
|------|--------|-------|
| Explorer shell | WIP | `WIP/explorer/` — Windows CE / PDA UI on MiniWin |
| BlackBerry shell | WIP | `WIP/blackberry/` — BB6-style homescreen on MiniWin |
| ClassicMac shell | WIP | `WIP/classicmac/` — Mac System 6-style shell |
| JC3248W535 port | WIP | Compiles, pins unverified on hardware |
| Waveshare 1.69" port | WIP | Compiles, pins unverified on hardware |
| T-Deck port | WIP | ST7789 driver + BB6 keyboard shell |
| App SDK | Planned | `.meow` MicroPython API docs + examples (see `Userland/`) |
| Kernel OTA | Idea | Safe factory partition update mechanism |
| Testing | Always needed | Hardware verification, flash compatibility |

Issues and PRs welcome.
