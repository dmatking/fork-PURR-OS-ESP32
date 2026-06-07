# PURR OS — Build Guide

Run all commands from this folder (`Builder/`). The script handles everything else.

---

## Prerequisites

### 1. ESP-IDF 5.1.x or 5.2.x (required)

> **IDF version constraint:** `arduino-esp32` 3.0.0 requires IDF **≥ 5.1.0 and < 5.3.0**.
> - IDF 6.x removed `wifi_provisioning` — **fails at cmake**.
> - IDF 5.3+ changed the ESP32-S3 touch driver API (`touch_value_t` removed) — **fails at compile**.
> - Use the latest available **5.1.x or 5.2.x** installer. Both are confirmed-working.

Download the **IDF 5.1.x or 5.2.x offline Windows installer** from the Espressif GitHub releases page
(`esp-idf-tools-setup-offline-5.2.x.exe`). It installs alongside any existing IDF version.
After install you will have an **"ESP-IDF 5.2 CMD"** shortcut in Start — use that for all
PURR OS builds.

Source it before every build session:

```bash
# Install (one-time) — download IDF 5.3.x from https://github.com/espressif/esp-idf/releases
# Then per session:
. $IDF_PATH/export.sh
```

On Windows, use the **ESP-IDF Command Prompt** (installed with the ESP-IDF Windows installer) — it sources the environment automatically. Then open Git Bash or WSL inside that context to run `build.sh`.

Alternatively, use the **ESP-IDF VS Code extension** which handles the environment for you.

### 2. Arduino-ESP32 component

Required by all targets. The `idf_component.yml` in `CoreOS/` declares it. The IDF Component Manager pulls it automatically on first build:

```bash
# Runs automatically, but you can trigger manually:
cd ../CoreOS && idf.py update-dependencies
```

Minimum version: `espressif/arduino-esp32 >= 3.0.0`

### 3. MicroPython submodule (full builds only)

Skip this for `--mini` builds. Required only if you want `.meow` Python apps to run.

```bash
cd ../CoreOS
git submodule add https://github.com/micropython/micropython.git components/micropython
cd components/micropython
git checkout v1.23.0
make -C mpy-cross
make -C ports/embed BOARD=ESP32S2
```

### 4. LoRa kernel (Heltec and T-Deck only)

CYD has no LoRa — skip for CYD builds.

The repo includes three interchangeable backends in `LoRa Kernels/`. Copy the right one:

```bash
# Heltec V3 and T-Deck → SX1262 via SPI
cp -r "../LoRa Kernels/SX1262/." ../CoreOS/system/kernel/modules/

# CattoBoardV1 PCB → RAK3172 via UART AT
cp -r "../LoRa Kernels/RAK3172/." ../CoreOS/system/kernel/modules/
```

> **Note:** The Heltec V3 has SX1262 LoRa built in. For best LoRa support, the Arduino IDE with the official **Heltec ESP32 Dev-Boards** board package includes the Heltec LoRa library and has been validated against the V3 hardware. If you are using ESP-IDF directly, use the SX1262 kernel above.

---

## Building

### Syntax

```bash
./build.sh [--target TARGET] [--mini] [--clean] [--flash PORT] [--monitor PORT]
```

| Flag | Description |
|---|---|
| `--target TARGET` | `heltec` (default), `cyd`, `tdeck` |
| `--mini` | Strips MicroPython runtime — faster build, smaller binary. Smol and launcher still work; `.meow` Python apps do not. |
| `--clean` | Wipes the build directory before building. Use after switching targets. |
| `--flash PORT` | Flash the device after building. Port is `COMx` on Windows, `/dev/ttyUSBx` on Linux. |
| `--monitor PORT` | Open serial monitor after flashing. `Ctrl+]` to exit. |

---

## Heltec WiFi LoRa 32 V3

**Chip:** ESP32-S3 | **Flash:** 8MB | **Display:** SSD1306 128×64 OLED | **LoRa:** SX1262

### First build (mini — recommended for first flash)

```bash
./build.sh --target heltec --mini
```

### Full build (with MicroPython .meow app support)

Requires MicroPython submodule (see Prerequisites §3) and SX1262 LoRa kernel (§4).

```bash
./build.sh --target heltec
```

### Build + flash

```bash
# Windows
./build.sh --target heltec --mini --flash COM5

# Linux / macOS
./build.sh --target heltec --mini --flash /dev/ttyUSB0
```

### Build + flash + monitor

```bash
./build.sh --target heltec --mini --flash COM5 --monitor COM5
```

### After flashing

The Heltec boots into **smol** — the text shell on the 128×64 OLED:
- `SELECT` (GPIO 0, BOOT button) — cycle through apps / confirm
- `BACK` (GPIO 47, USR button) — open PURR menu

PURR menu: **About**, **System Info**, **Quit PURR OS** (restarts).

Serial monitor baud: **115200**. With `verbose: true` in `heltec.json` (default), KITT logs every boot step to serial.

### Switching between builds (important)

Always `--clean` when switching from full → mini or changing targets. Stale CMake cache will use the wrong source list:

```bash
./build.sh --target heltec --mini --clean
```

---

## CYD (ESP32-2432S028R)

**Chip:** ESP32 | **Flash:** 4MB | **Display:** ILI9341 320×240 | **Touch:** XPT2046

### Build

```bash
./build.sh --target cyd
```

Mini build works too (launcher doesn't use MicroPython):

```bash
./build.sh --target cyd --mini
```

### Build + flash

```bash
# Windows
./build.sh --target cyd --mini --flash COM5

# Linux / macOS
./build.sh --target cyd --mini --flash /dev/ttyUSB0
```

### After flashing

The CYD boots into the **PURR OS Launcher** — a dark-themed card UI:

- Each OTA slot shows as a card (LAUNCH / DELETE if occupied, INSTALL if empty)
- **INSTALL** → file picker from SD card root (`.bin` / `.purr` files)
- **LAUNCH** → flashes OTA partition as boot target and restarts into that firmware
- Return to PURR OS: the launched firmware must call `esp_ota_set_boot_partition(factory)` + `esp_restart()`, or hold BOOT pin at power-on for the emergency flasher

**SD card** (optional, for firmware installs): VSPI bus, CS=5, CLK=18, MOSI=23, MISO=19.  
Format as FAT32, place `.bin` or `.purr` files in the root.

### LVGL configuration

CYD builds require LVGL. The `cyd.defaults` sdkconfig sets `CONFIG_LV_MEM_SIZE_KILOBYTES=32`. If LVGL complains about a missing `lv_conf.h`, run:

```bash
cd ../CoreOS
idf.py menuconfig
# Navigate: Component config → LVGL → configure as needed
```

---

## T-Deck / T-Deck Plus (LilyGo)

**Status: work in progress.** Both variants compile (smol shell as placeholder) but the BB OS 6 shell and ST7789 display driver are not yet wired.

### T-Deck (original)

```bash
./build.sh --target tdeck --mini
```

### T-Deck Plus

T-Deck Plus adds a **u-blox MIA-M10Q GNSS module** and a **larger battery**. Pass `--tdeck-plus` to enable the `PURR_IS_TDECK_PLUS` preprocessor define, which gates GPS and battery-manager code:

```bash
./build.sh --target tdeck --tdeck-plus --mini
```

PowerShell:

```powershell
.\Build.ps1 -Target tdeck -TdeckPlus -Mini
```

The interactive wizard asks "Normal or Plus?" automatically when T-Deck is selected as the target.

| Define | When set |
|---|---|
| `PURR_IS_TDECK_PLUS` | `--tdeck-plus` / `-TdeckPlus` flag, or wizard selection |

#### MIA-M10Q hardware notes

| Item | Detail |
|---|---|
| UART pins | GPIO44 = RX (from module), GPIO43 = TX (to module) — uses Serial1 |
| Baud rate | 9600 (default); falls back to 4800 on init probe |
| Former function | These are the Grove interface pins — Grove is unavailable on T-Deck Plus |
| Detection quirk | Module needs ~2 s after power-on before NMEA output begins. Probing too quickly causes false "no GPS present" (observed in Meshtastic firmware). The PURR GPS init waits `GPS_INIT_WAIT_MS` (2000 ms) before probing to avoid this. |

---

## Meshtastic Co-Resident Stack

**Status: scaffold only.** The `mesh_manager` module is wired into the build system. The Meshtastic submodule and its ESP-IDF component wrapper must be added before `--with-mesh` builds will compile.

### Prerequisites

Clone the Meshtastic firmware submodule:

```bash
cd ../CoreOS
git submodule add https://github.com/meshtastic/firmware.git components/meshtastic
cd components/meshtastic
git checkout v2.5.x   # use latest stable tag
```

Create `CoreOS/components/meshtastic/CMakeLists.txt` (ESP-IDF component wrapper):

```cmake
idf_component_register(
    SRCS
        "src/MeshService.cpp"
        "src/NodeDB.cpp"
        "src/Router.cpp"
        "src/FloodingRouter.cpp"
        "src/RadioLibInterface.cpp"
        "src/SX1262Interface.cpp"
        "src/PowerFSM.cpp"
        # add further src/ files as link errors reveal them
    INCLUDE_DIRS "src" "src/mesh" "src/gps"
    REQUIRES espressif__arduino-esp32 nvs_flash spiffs
)
```

### Building with Mesh

```bash
# Heltec V3 — mini build (no MicroPython)
./build.sh --target heltec --mini --with-mesh

# T-Deck Plus
./build.sh --target tdeck --tdeck-plus --with-mesh
```

PowerShell:

```powershell
.\Build.ps1 -Target heltec -Mini -WithMesh
.\Build.ps1 -Target tdeck -TdeckPlus -WithMesh
```

The interactive wizard shows **Meshtastic** as a toggleable module on LoRa targets (hidden on CYD).

### Radio ownership

`mesh_manager_start()` calls `lora_manager_yield()` to release the SX1262 to Meshtastic. `mesh_manager_stop()` calls `lora_manager_reclaim()` to hand it back. Do not call KITT LoRa APIs while mesh is running.

### App Manager integration

```cpp
// Start mesh from any app or C++ code:
mesh_manager_start();

// Stop:
mesh_manager_stop();

// Send a broadcast message:
mesh_manager_send("Hello mesh", MESH_BROADCAST_ADDR);

// Poll for incoming messages:
mesh_packet_t pkt;
while (mesh_manager_recv(&pkt)) {
    Serial.printf("From 0x%08X: %s\n", pkt.from, pkt.text);
}
```

### RAM budget

| Target | Internal RAM | PSRAM | Verdict |
|---|---|---|---|
| Heltec V3 | ~512KB | None | Tight — PURR + Meshtastic ~400KB combined |
| T-Deck Plus | ~512KB | 8MB | Comfortable — mesh state goes in PSRAM |

### Troubleshooting

| Problem | Fix |
|---|---|
| `MeshService.h not found` | Clone the Meshtastic submodule (see above) |
| `meshtastic: unknown component` | Ensure `components/meshtastic/CMakeLists.txt` exists and calls `idf_component_register` |
| Linker errors from Meshtastic src | Add the missing `.cpp` files to the CMakeLists SRCS list |
| Radio init fails at runtime | Ensure `--with-mesh` is not combined with `--no-lora`; lora_manager must be enabled |

---

## Partition Tables

Each target has its own partition CSV in `CoreOS/`. The build script selects the correct one via `sdkconfig.defaults`.

### Heltec — 8MB

| Partition | Offset | Size | Notes |
|---|---|---|---|
| nvs | 0x9000 | 20KB | Settings, NVS heartbeat |
| otadata | 0xe000 | 8KB | OTA selector |
| factory | 0x10000 | 2MB | PURR OS (permanent) |
| ota_0 | 0x210000 | 2MB | Firmware slot 0 |
| ota_1 | 0x410000 | 2MB | Firmware slot 1 |
| spiffs | 0x610000 | ~1.9MB | App config, keymaps, crash logs |

### CYD — 4MB

| Partition | Offset | Size | Notes |
|---|---|---|---|
| nvs | 0x9000 | 20KB | |
| otadata | 0xe000 | 8KB | |
| factory | 0x10000 | 1.25MB | PURR OS Launcher (permanent) |
| ota_0 | 0x150000 | 1.375MB | Firmware slot 0 |
| ota_1 | 0x2B0000 | 1MB | Firmware slot 1 |
| spiffs | 0x3B0000 | 320KB | |

---

## Flashing Manually (without build.sh)

If you built with `idf.py` directly or need to re-flash without rebuilding:

```bash
cd ../CoreOS
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

Flash only the app (skip bootloader and partition table — useful for iteration):

```bash
idf.py -p COM5 app-flash
```

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `IDF_PATH not set` | Source ESP-IDF: `. $IDF_PATH/export.sh` |
| `wifi_provisioning: unknown name` | You are using IDF 6.x — install IDF 5.1.x (see Prerequisites §1) |
| `Touch IDF driver Not supported` or `touch_value_t` error | You are using IDF 5.3+ — install IDF 5.1.x (see Prerequisites §1) |
| `Unknown chip type` or wrong target | Run `./build.sh --target <name> --clean` — stale target in sdkconfig |
| `micropython_embed.h not found` | Either clone the MicroPython submodule or use `--mini` |
| LVGL `lv_conf.h` error | Run `idf.py menuconfig` in `CoreOS/` to configure LVGL via Kconfig |
| Touch not responding on CYD | Calibration may need tuning — edit `X_RAW_MIN/MAX` and `Y_RAW_MIN/MAX` in `touch_xpt2046.cpp` |
| Heltec LoRa init fails | Verify `lora_pins` in `heltec.json` match your board revision; copy SX1262 kernel (see Prerequisites §4) |
| `CMake Error: Unknown TARGET_DEVICE` | You passed an invalid `--target`. Valid: `heltec`, `cyd`, `tdeck` |
| Build works but app crashes immediately | Check serial monitor — KITT prints each boot step. First failing step has an `ERR:` prefix |

---

## Common Workflows

### Iterate fast on Heltec

```bash
# First time: full clean + flash
./build.sh --target heltec --mini --clean --flash COM5 --monitor COM5

# Subsequent: skip clean, just re-flash
./build.sh --target heltec --mini --flash COM5
```

### Switch from Heltec to CYD

```bash
./build.sh --target cyd --mini --clean --flash COM5
```

### Verify LoRa on Heltec without flashing

Set `"verbose": true` in `CoreOS/system/kernel/devices/heltec.json`, then check serial output. Step 11 will log either `[KITT] LoRa OK` or `[KITT] WARN: LoRa init failed`.
