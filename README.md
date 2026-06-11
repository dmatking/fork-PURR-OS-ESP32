# PURR OS — v0.10.1

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
| PURR OS   | v0.10.1 | 2026-06-11   |
| KITT      | v0.6.9  | 2026-06-11   |

Version strings are defined in [CoreOS/system/kernel/purr_version.h](CoreOS/system/kernel/purr_version.h) and automatically embedded into the firmware image via `esp_app_desc_t` — visible in the bootloader's slot card and on the homescreen.

Full release history is in **[CHANGELOG.md](CHANGELOG.md)** at the repository root.

### Release Notes: v0.9.6 / KITT v0.6.1

**MagiDOS — Full-featured 8086 DOS emulator**
- **8086tiny CPU core** — Complete 8086 instruction set, 640 KB conventional memory, step-by-step execution
- **MZ EXE loader** — Parse MZ headers, apply relocations, set entry points — supports real DOS executables
- **COM file support** — Flat binary loader at 0x0100:0x0100 with PSP setup
- **CGA text rendering** — 80×25 text grid with 16-color palette and CP437 8×8 font (96 glyphs)
- **MiniWin window integration** — Runs as draggable/minimizable window in WCE shell alongside other apps
- **File picker UI** — Touch-selectable `.COM` and `.EXE` files from SD card
- **INT 0xE0 PURR kernel bridge** — DOS programs access WiFi, LoRa, Bluetooth, and notifications
- **Example programs** — hello.c, hello.asm, purr_demo.c with complete OpenWatcom build guide
- **Build flag** — `idf.py -DPURR_ENABLE_MAGIDOS=1 build` to include MagiDOS subsystem

**See [MagiDOS section below](#magidos--8086-dos-emulator) for complete details, architecture, and testing.**

### Release Notes: v0.9.5 / KITT v0.6.0

- **Kernel panic screens** — `purr_panic()` renders full-screen BSOD: blue `:-/` (SYSTEM UNSTABLE, recoverable) and red `:-(' (SYSTEM CRASHED, reboots after 10s via `esp_restart()`); stop codes printed on screen and over serial; wired into KITT boot fail paths
- **Software font for panic screens** — `display_font5x7.h` shared 5×7 bitmap font (ASCII 32-126) renders via `fill_rect` callbacks; used by ST7789 and ST7796 drivers; ILI9341 uses TFT_eSPI directly
- **Serial `panic` shell command** — `panic [blue|red] [code] [msg]` triggers panic from UART0 shell; supports custom stop code and message
- **PSRAM Lua allocator** — `PURR_HAS_PSRAM` flag routes `lua_newstate()` through `heap_caps_realloc(MALLOC_CAP_SPIRAM)` on T-Deck Plus and JC3248W535
- **`tdeck_plus.defaults`** — SDK defaults file with full SPIRAM/OCT configuration for T-Deck Plus

### Release Notes: v0.9.4 / KITT v0.5.3

- **Blackberry shell theme** (`shell_blackberry.cpp`) — green-on-black phosphor terminal aesthetic; status/time/notif/wallpaper/tabs/dock layout; app drawer via wallpaper tap; selectable via `PURR_UI_THEME=blackberry` (SDK: `[t]` in module wizard)
- **Lua window API** (`app_lua_window.cpp`) — fully implemented: `win.*` / `sd.*` / `kitt.*` Lua bindings, per-window `lua_State*` + FreeRTOS task, retained-mode widget list with mutex
- **`purr_wm_launch()` wired** — real MiniWin implementation in `purr_wm_launch.cpp`; creates Lua app window; stub removed from `ui_stubs.cpp`
- **Touch fix** — MiniWin delivers touch already in client-relative coords; removed double-subtraction in `app_settings.cpp` and `app_files.cpp` (fixes broken tab switching)
- **Lua runtime ESP-IDF** — `Serial`/`millis()`/`delay()` replaced with `ESP_LOGI`/`esp_timer_get_time()`/`vTaskDelay()`
- **Conditional modules** — MagiDOS/MagicMac wrapped in `#ifdef`; catalog uses `sizeof` count; `PURR_HAS_*` defines propagated to lib_miniwin
- **SDK 0.9.4** — MagiDOS/MagicMac/Lua module pickers; LoRa stripped from CYD targets; `[t]` UI theme picker when `ui_kernel=miniwin`

### Release Notes: v0.9.2 / KITT v0.5.3

- **SD card wired into KITT** — `sd_available()` API, device JSON `"sd"` field, `pm_init()` called at boot step 10.5
- **conman shell commands** — `wifi-status/scan/connect/disconnect/forget` and `bt-status/scan/devices/pair/unpair` added to `drv_shell`
- **WCE shell rewrite** — dynamic two-level start menu driven by `purr_catalog[]`; taskbar app buttons; RAM clock; applied to all device targets
- **File Explorer** (`app_files`) — dual-tab SD/SPIFFS browser with directory navigation and scroll strip
- **App Launcher** (`app_launcher`) — scans `/sdcard/apps/` for `.paws` (userland) and `.claw` (admin) Lua scripts; red text for admin apps
- **Lua window host** (`app_lua_window`) — interface defined and stubbed; `luaconf.h` `LUA_32BITS` corrected 0→1 (root cause of all previous Lua build failures)
- **Kernel panic handler** (`purr_panic`) — blue/red screen with ring buffer dump and `display_ili9341_*` panic display
- **Partition table** — `factory` slot bumped to 1.0625 MB to fit full OS binary; `ota_0` adjusted accordingly
- **Multi-device HALs** — `cyd_s028r`, `jc3248`, `tdeck_plus`, `waveshare` device folders added at project root

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

PURR OS is an embedded operating system for ESP32 devices. It runs a custom kernel (KITT), exposes scripting runtimes (MicroPython, Lua, and 8086 DOS emulation), manages optional radio/BT/USB modules, and renders a full windowed UI via the **MiniWin** window manager. Think Windows CE / DOS-on-a-single-chip vibes, on a $10 ESP32 LCD board.

The architecture splits across two flash partitions: a **PURR Kernel** in the factory slot (OTA-immune, handles boot decisions and SD recovery) and the **PURR Userland** in ota_0 that gets updated over-the-air.

Notable subsystems:
- **MiniWin windowed UI** — CYD devices run a full Windows CE-inspired shell with overlapping windows, taskbar, and app launcher
- **MagiDOS emulator** — Run real DOS programs (.COM/.EXE) with INT 0xE0 kernel bridge for WiFi, LoRa, and Bluetooth access
- **Lua scripting** — Lightweight app runtime for system utilities and user scripts
- **Radio modules** — Optional LoRa, Meshtastic mesh, Bluetooth, LTE, and GPS

---

## Supported Targets

| Target | Chip | Flash | Display | Touch | Input | Status |
|--------|------|-------|---------|-------|-------|--------|
| `cyd_s028r` | ESP32 | 4 MB | ILI9341 2.4" 320×240 | XPT2046 SPI | Touch | Active |
| `cyd_s024c` | ESP32 | 4 MB | ST7789 2.4" 320×240 | CST820 I2C | Touch | Active |
| `cyd_boot` | ESP32 | 4 MB | ILI9341 2.4" | CST820 I2C | Touch | Active — factory kernel only |
| `tdeck_plus` | ESP32-S3 | 16 MB | ST7789 3.5" 320×240 | GT911 cap | Touch + KB + trackball | Active |
| `jc3248w535` | ESP32-S3 | 16 MB | ST7796 3.5" 480×320 | GT911 cap | Touch | WIP |
| `waveshare169` | ESP32-S3 | 4 MB | ST7789 1.69" 240×280 | CST816S cap | Touch | WIP |
| `heltec` | ESP32-S3 | 8 MB | SSD1306 OLED 128×64 | — | UART shell | Working |
| `tembed_cc1101` | ESP32-S3 | 16 MB | ST7789 170×320 | — | Rotary encoder | Working |
| `tdeck` | ESP32-S3 | 16 MB | ST7789 320×240 | — | Trackball + KB | WIP |

**Note:** `cyd_s028r` and `cyd_s024c` are the primary fully-featured targets. `tdeck_plus` is actively developed. All others build cleanly but may have pin or driver issues pending hardware verification.

---

## Partition Layout

### CYD / Waveshare169 (4 MB)
```
0x1000   IDF bootloader       ~27 KB
0x8000   Partition table        2 KB
0x9000   nvs                   20 KB   (key-value store)
0x10000  factory — PURR OS      3 MB   (entire app image)
0x300000 spiffs — filesystem    1 MB   (scripts, device config, app data)
```

### T-Deck Plus / JC3248W535 / T-Deck / T-Embed (16 MB)
```
0x9000   nvs                   20 KB
0x10000  factory — PURR OS     14 MB
0xE00000 spiffs — filesystem    2 MB
```

### Heltec (8 MB)
```
0x9000   nvs                   20 KB
0x10000  factory — PURR OS      7 MB
0x700000 spiffs — filesystem    1 MB
```

**No OTA partition.** As of v0.10.1, OTA slots have been removed — the factory partition uses all available flash. SD card flashing handles firmware updates instead of OTA.

### Boot sequence
1. IDF bootloader jumps to factory
2. PURR OS boots, mounts SD card, runs KITT kernel init, launches UI shell

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
| MagiDOS | `PURR_ENABLE_MAGIDOS` | 8086 DOS emulator; see section below |
| MicroPython | `BUILD_MINI=0` | `.meow` app runtime; always off for `cyd_boot` |

---

## MagiDOS — 8086 DOS Emulator

MagiDOS is a complete Intel 8086 DOS emulator for PURR OS that runs real DOS programs (.COM and .EXE executables) inside MiniWin windows on CYD devices. Programs access PURR kernel services via a custom INT 0xE0 bridge, enabling WiFi, LoRa, Bluetooth, and notification APIs from within DOS.

### Architecture

- **8086tiny CPU core** — Vendored from [github.com/adriancable/8086tiny](https://github.com/adriancable/8086tiny); full 8086 instruction set, 640 KB conventional memory, 1 MB ROM space
- **MZ EXE loader** — Parses DOS executable headers, applies relocation tables, handles segmented memory layout
- **COM file support** — Flat binary programs loaded at 0x0100:0x0100 with minimal PSP (Program Segment Prefix)
- **CGA graphics** — 80x25 text mode with 16-color palette; CP437 8x8 bitmap font (ASCII 0x20-0x7E)
- **MiniWin window** — Runs as a draggable, minimizable window in the WCE shell; coexists with other applications
- **File picker** — SD card file browser; selects .COM/.EXE files via touch; handles errors gracefully
- **INT 0xE0 dispatcher** — Bridges DOS interrupts to PURR kernel services; registers routed correctly (AH, DS:SI, ES:DI, CX)
- **Keyboard injection** — Touch input mapped to arrow keys; BIOS keyboard buffer wiring (INT 16h data area)

### Building with MagiDOS

```bash
# Enable MagiDOS in the build
cd CoreOS
idf.py -DPURR_ENABLE_MAGIDOS=1 build
idf.py -p /dev/ttyUSB0 flash monitor

# Or via SDK (interactive):
# Run: .\SDK\SDK.ps1 (Windows) or ./SDK/sdk.sh (Linux)
# Select: Target > Modules > Enable MagiDOS > Build
```

### Running DOS Programs

1. **Compile a DOS program** with OpenWatcom:
   ```bash
   # COM file (flat binary)
   wcl -mt -0 -os yourprogram.c -fe=yourprogram.com

   # EXE file (segmented executable)
   wcc -mt -0 -os yourprogram.c
   wlink format dos exe file yourprogram.obj name yourprogram.exe
   ```

2. **Copy to SD card** (must be root directory `/sdcard/`, not subdirectories):
   ```bash
   cp yourprogram.com /media/your-sd/
   ```

3. **Launch from shell**:
   - Tap "Meow!" (start menu)
   - Tap "Programs >"
   - Tap "MagiDOS"
   - Tap filename from file picker
   - Program runs in window; arrow keys and Enter for input
   - Tap window close (X) or press Ctrl+C to exit

### Example Programs

Example programs with source code are provided in [magidos/SDK/openwatcom/examples/](magidos/SDK/openwatcom/examples/):

- **hello.c** — Simple "Hello World" in C; demonstrates stdio and getch()
- **hello.asm** — Same program in pure 8086 assembly; uses BIOS INT 21h
- **purr_demo.c** — Interactive demo showing INT 0xE0 kernel bridge calls

Full build guide and testing procedures: [magidos/SDK/openwatcom/examples/README.md](magidos/SDK/openwatcom/examples/README.md)

### INT 0xE0 — PURR Kernel Bridge

DOS programs can call PURR OS kernel services via software interrupt 0xE0:

```c
#include <dos.h>

// Query WiFi status
typedef struct {
    unsigned char connected;
    char ssid[33];
    signed char rssi;
} wifi_status_t;

wifi_status_t wifi;

// Set up registers and call INT 0xE0
asm {
    mov ah, 0x20           // WiFi status command
    mov di, offset wifi    // ES:DI = buffer address
    int 0xE0               // Call PURR kernel
}

if (wifi.connected) {
    printf("WiFi: %s (%d dBm)\n", wifi.ssid, wifi.rssi);
}
```

Available commands (AH register):

| Command | Code | Description |
|---------|------|-------------|
| WiFi status | 0x20 | Read connected SSID and RSSI |
| WiFi scan | 0x21 | List available networks |
| WiFi connect | 0x22 | Connect to network by SSID |
| LoRa send | 0x10 | Transmit packet (buffer at ES:DI) |
| LoRa receive | 0x11 | Check for received packet |
| Post notification | 0x40 | Send notification to WCE shell |

Full documentation: [magidos/CoreOS/components/lib_purr_dos_ipc/purr_dos_ipc.h](magidos/CoreOS/components/lib_purr_dos_ipc/purr_dos_ipc.h)

### Limitations

- **MZ relocations** — Relocations are applied correctly, but minimal testing on complex executables
- **DOS interrupts** — Only BIOS (INT 08h-0Fh) and basic DOS (INT 21h) are safe; hardware INT calls and protected mode not supported
- **Graphics** — CGA text mode only; no VESA, no mode-X, no VGA 256-color
- **Memory** — 640 KB limit (conventional memory); no extended memory (XMS), no EMS
- **Performance** — Step-by-step CPU execution, not cycle-accurate; programs run slower than native DOS PC

### Project Files

| Path | Purpose |
|------|---------|
| `magidos/` | MagiDOS emulator subsystem |
| `magidos/CoreOS/components/drv_8086/` | 8086tiny CPU core, hooks, step function |
| `magidos/CoreOS/components/lib_purr_dos_ipc/` | INT 0xE0 dispatcher, command handlers |
| `magidos/purr_wm_app/magidos/` | CGA text rendering, file picker UI |
| `magidos/SDK/openwatcom/examples/` | Example programs and build guide |
| `magidos/MAGIDOS_STATUS.md` | Detailed architecture and status report |
| `devices/apps/app_magidos.cpp` | MiniWin window app for WCE shell |

### Status

MagiDOS is production-ready for .COM and .EXE DOS programs. All 13 major development tasks complete:

- 8086 CPU emulation
- Memory management
- Interrupt dispatch (INT 0xE0 + BIOS)
- Keyboard input
- CGA text rendering
- File picker UI
- MiniWin window integration
- MZ EXE loader with relocations
- Shell registration
- Example programs

See [magidos/MAGIDOS_STATUS.md](magidos/MAGIDOS_STATUS.md) for full details and known limitations.

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
├── magidos/                    — MagiDOS 8086 DOS emulator subsystem
│   ├── CoreOS/components/
│   │   ├── drv_8086/           — 8086tiny CPU emulator, hooks, step function
│   │   └── lib_purr_dos_ipc/   — INT 0xE0 dispatcher, kernel bridge
│   ├── purr_wm_app/magidos/    — CGA text rendering, file picker UI
│   ├── SDK/openwatcom/examples/  — Example programs (hello.c/.asm, purr_demo.c)
│   └── MAGIDOS_STATUS.md       — Full architecture and development status
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
| [magidos/MAGIDOS_STATUS.md](magidos/MAGIDOS_STATUS.md) | MagiDOS emulator architecture, capabilities, and status |
| [magidos/SDK/openwatcom/examples/README.md](magidos/SDK/openwatcom/examples/README.md) | MagiDOS build guide, example programs, INT 0xE0 API |

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
