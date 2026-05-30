# PURR OS for C — Architecture Overview

## What It Is

PURR OS for C is a modular embedded OS for ESP32-S3 devices, written in C/C++ and built via Arduino IDE with official Heltec LoRa library support. It targets devices from the Heltec V3 (8MB flash, 512KB SRAM) up to the CattoPad cyberdeck (16MB flash, 8MB PSRAM). The same codebase runs on all targets — hardware differences are declared in `device.json`, not in code.

The OS is built around four principles:
- **KITT is the only code that touches hardware.** Everything else calls KITT APIs.
- **One app at a time.** Lightweight overlays are the only exception.
- **Drag and drop everything.** Apps (.meow), native binaries (.purr), and third-party firmware (.bin) are all drop-in.
- **Nothing hard-fails.** KITT always has a text mode fallback. watchdog always has a recovery path.

---

## Stack Layers (bottom to top)

```
┌──────────────────────────────────────────────┐
│        User Apps (.meow / .purr bundles)      │  ← notes, calc, msn, controlpanel
├──────────────────────────────────────────────┤
│           explorer.meow  (UI shell)           │  ← Windows CE style, LVGL-based
├──────────────────────────────────────────────┤
│           system.meow   (orchestration)       │  ← app lifecycle, OTA handoff, memory
├──────────────────────────────────────────────┤
│           bridge.meow   (translation)         │  ← key mapping, radio handoff brokering
├──────────────────────────────────────────────┤
│         KITT Kernel      (hardware HAL)       │  ← display, radios, GPIO, process exec
├──────────────────────────────────────────────┤
│         watchdog.bin     (boot guardian)      │  ← spawns KITT, owns restart authority
├──────────────────────────────────────────────┤
│       ESP-IDF Bootloader (native)             │  ← reads NVS boot flags, loads watchdog
└──────────────────────────────────────────────┘
```

Each layer only calls the layer directly below it. explorer.meow never calls WiFi.begin(). system.meow never touches SPI. Only KITT does.

---

## Key Design Rules

### 1. KITT Owns All Hardware
Display SPI, GPIO pins, WiFi stack, BT stack, LoRa radio — all initialised and managed exclusively by KITT. Upper layers read and write through KITT's API (see 02_KITT_Kernel_Spec.md). This means a crash in explorer.meow or system.meow cannot corrupt hardware state. KITT keeps running, holds the display, and renders a text fallback.

### 2. Single App Model
One fullscreen app runs at a time. Attempting to launch a second fullscreen app while one is running produces an explorer.meow dialog. The only exceptions are **lightweight overlay apps**:

| App | Type |
|---|---|
| notes.meow | Lightweight overlay |
| calc.meow | Lightweight overlay |
| msn.meow | Lightweight overlay |
| controlpanel.meow | Lightweight overlay |
| keyboard.meow | Lightweight overlay (input method) |

Lightweight overlays float on top of explorer.meow. They do not count as a fullscreen app and can run alongside each other (up to 3 in the taskbar at once). They are blocked during `/friends/` firmware exclusivity unless the device has enough free RAM (declared in `device.json` as `friends_ram_threshold_kb`).

### 3. /friends/ Gets Radio Exclusivity
When Meshtastic or Bruce launches, it requests ownership of its required radios (WiFi, BT, LoRa). KITT yields those radios via the handoff protocol. No other app can request those radios until the firmware exits. KITT always retains: display, keypad, touch, battery monitoring, and the reserved key combo (force-kill).

### 4. Everything Is Swappable
- explorer.meow can be replaced with any UI shell that implements the contract calls
- KITT modules load conditionally from device.json — swap a module, drop in the new bundle
- bridge.meow is independently updatable without touching KITT
- watchdog validates a new KITT bundle before committing — rolls back to known-good on failure

### 5. Graceful Degradation
```
explorer.meow crashes  →  system.meow relaunches it, passes crash report
system.meow crashes    →  watchdog signals KITT to restart it
KITT hangs             →  watchdog restarts KITT (heartbeat timeout)
KITT unflashable       →  emergency.bin takes over (no KITT needed)
```

---

## device.json — Hardware Profile

Every device has one. KITT reads it on boot before loading anything else. This single file controls which modules load, what the UI dimensions are, and what radios are available.

### CattoPad (16MB, full build)
```json
{
  "device": "cattopad",
  "display": "ili9488",
  "display_res": [320, 480],
  "touch": "mxt336t",
  "psram": true,
  "flash_mb": 16,
  "ram_kb": 512,
  "psram_mb": 8,
  "pi_slot": true,
  "radios": ["wifi", "bt", "lora"],
  "cpu_max_mhz": 240,
  "lora_region": "US915",
  "verbose_boot": false,
  "boot_splash": "assets/splash_cattopad.txt",
  "keymap": "keymaps/cattopad_4x5.json",
  "friends_ram_threshold_kb": 1024
}
```

### Heltec V3 (8MB, minimal build)
```json
{
  "device": "heltec_v3",
  "display": "ssd1306",
  "display_res": [128, 64],
  "touch": "none",
  "psram": false,
  "flash_mb": 8,
  "ram_kb": 512,
  "psram_mb": 0,
  "pi_slot": false,
  "radios": ["wifi", "bt", "lora"],
  "cpu_max_mhz": 240,
  "lora_region": "US915",
  "verbose_boot": true,
  "boot_splash": "assets/splash_v3.txt",
  "keymap": "keymaps/heltec_v3_2btn.json",
  "friends_ram_threshold_kb": 0
}
```

`friends_ram_threshold_kb`: if device free RAM exceeds this when a `/friends/` firmware launches, lightweight overlays are permitted. Set to 0 to block all overlays during firmware exclusivity.

---

## Module Loading at Boot

KITT reads device.json and loads only the modules the device actually has. Modules for absent hardware are never compiled in or loaded, keeping the flash footprint minimal.

| Module | File | Loaded when |
|---|---|---|
| ILI9488 display | `display_ili9488.cpp` | `"display": "ili9488"` |
| SSD1306 display | `display_ssd1306.cpp` | `"display": "ssd1306"` |
| mXT336T touch | `touch_mxt336t.cpp` | `"touch": "mxt336t"` |
| LoRa radio | `lora_manager.cpp` | `"lora"` in radios |
| WiFi | `wifi_manager.cpp` | `"wifi"` in radios |
| Bluetooth | `bt_manager.cpp` | `"bt"` in radios |
| Pi manager | `pi_manager.cpp` | `"pi_slot": true` |
| MTP USB | `mtp_manager.cpp` | Always loaded |
| PSRAM | `psram_manager.cpp` | `"psram": true` |
| Power manager | `power_manager.cpp` | Always loaded |

---

## Window & App Rules

| App Type | Can Run Alongside | Blocked By |
|---|---|---|
| Fullscreen firmware (`/friends/`) | Nothing | Any other fullscreen app |
| Fullscreen user app | Nothing | Firmware, other fullscreen apps |
| Lightweight overlay | Other lightweight overlays (max 3 taskbar) | Fullscreen firmware (unless RAM threshold met) |

Explorer dialog when exclusivity conflict occurs:
> "Meshtastic is already running. Close it before launching Bruce?"

---

## Binary Formats

| Extension | What It Is | Where It Lives |
|---|---|---|
| `.meow` | App bundle — compiled C binary + manifest.json in a folder | `/apps/`, `/system/` |
| `.purr` | Native PURR OS binary — compiled C executable, same format as .bin | `/apps/purr/`, `/friends/` |
| `.bin` | Third-party firmware (Meshtastic, Bruce) | `/friends/` |
| `.fw` | Alternate firmware extension, treated identically to .bin | `/friends/` |

KITT identifies executables by binary header, not extension. `.purr` is a renamed `.bin`. Build your sketch in Arduino IDE, rename the output `.purr`, drop it in `/apps/purr/` — KITT finds it on next scan and adds it to the Start menu automatically.

### .meow Bundle Structure
```
myapp.meow/
├── main.bin         # Compiled C executable (Arduino build output)
├── manifest.json    # App metadata
└── assets/          # Icons, sounds, data files (optional)
```

manifest.json:
```json
{
  "name": "My App",
  "version": "1.0.0",
  "author": "you",
  "is_lightweight": true,
  "needs_wifi": false,
  "needs_bt": false,
  "needs_lora": false,
  "min_ram_kb": 64,
  "icon": "assets/icon.bmp"
}
```

---

## Build Environment

### Recommended: Arduino IDE (best LoRa support)

**Arduino IDE is the recommended build environment for PURR OS**, particularly for any target with LoRa. The official Heltec board package includes a validated SX1262 library that is tested against Heltec V3 hardware and handles radio initialisation, frequency configuration, and packet TX/RX without manual register programming.

#### Arduino IDE setup

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. Add the Heltec board manager URL:
   - `File → Preferences → Additional Boards Manager URLs`
   - Paste: `https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series/releases/download/0.0.7/package_heltec_esp32_index.json`
3. Install the board package:
   - `Tools → Board → Boards Manager` → search `Heltec ESP32` → install **Heltec ESP32 Series Dev-boards**
4. Install libraries via `Tools → Manage Libraries`:

| Library | Version | Purpose |
|---|---|---|
| `Heltec ESP32 Dev-Boards` | latest | SX1262 LoRa driver + OLED — **required for LoRa** |
| `lvgl` | 8.3.x | UI framework (ILI9341, ILI9488 targets) |
| `TFT_eSPI` | latest | Display driver for ILI9341 / ILI9488 |
| `ArduinoJson` | 7.x | device.json + keymap parsing |

#### Selecting your board in Arduino IDE

| Target | Arduino board selection |
|---|---|
| Heltec WiFi LoRa 32 V3 | `Heltec ESP32 Series Dev-boards → WiFi LoRa 32(V3)` |
| CYD (ESP32-2432S028R) | `ESP32 Arduino → ESP32 Dev Module` |
| T-Deck | `ESP32S3 Dev Module` |

> **LoRa note:** The Heltec board package's LoRa library is calibrated for their specific SX1262 circuit (TCXO, RF switch, DIO1 wiring). Using a generic SX1262 Arduino library on a Heltec V3 may fail or produce incorrect output. Always prefer the Heltec-provided library on Heltec hardware.

---

### Alternative: ESP-IDF + build.sh

For automated or CI builds, use the `Builder/` scripts. See `Builder/HOWTO.md` for full instructions. The ESP-IDF path uses the Arduino-ESP32 component layer (`espressif/arduino-esp32 >= 3.0.0`) so the same library code runs either way.

```bash
cd Builder
./build.sh --target heltec --mini --flash COM5
```

The `Builder/` path does **not** use the Heltec board package's LoRa library — it uses the `LoRa Kernels/SX1262/` drop-in instead. Both work; the Heltec board package path is easier for first-time setup and hardware debugging.

---

## Flash Partition Layout

| Region | Size | Contents |
|---|---|---|
| Bootloader | ~256KB | ESP-IDF bootloader, reads NVS boot flags |
| NVS | ~200KB | Settings, boot flags, heartbeat keys, hardware config |
| OTA staging | ~4MB | PSRAM-assisted staging area for firmware flashing |
| `/boot/` | ~512KB | watchdog.bin, emergency.bin, boot flags |
| `/system/` | ~4–5MB | KITT, system.meow, bridge.meow, explorer.meow |
| `/apps/` | ~1MB | User .meow bundles and .purr binaries |
| `/friends/` | ~4–6MB | Meshtastic, Bruce, other third-party firmware |
| Headroom | ~1–2MB | Future apps, assets, overflow |
| **Total** | **~16MB** | |

### V3 Flash Budget (8MB)
| Region | Size |
|---|---|
| Bootloader + NVS | ~0.5MB |
| KITT core + SSD1306 module + LoRa lib | ~0.6MB |
| system.meow + bridge.meow | ~0.4MB |
| Meshtastic | ~2–2.5MB |
| Remaining | ~4–4.5MB headroom |

---

## Full Folder Layout

```
/
├── boot/
│   ├── watchdog.bin           # First to run — spawns KITT, owns restart authority
│   └── emergency.bin          # KITT-independent recovery — MTP + flash from bare metal
│
├── system/
│   ├── kernel.meow/
│   │   ├── main.cpp           # KITT entry point
│   │   ├── kitt.h             # Public API header
│   │   ├── device.json        # Hardware profile
│   │   ├── modules/
│   │   │   ├── display_ili9488.cpp
│   │   │   ├── display_ssd1306.cpp
│   │   │   ├── touch_mxt336t.cpp
│   │   │   ├── wifi_manager.cpp
│   │   │   ├── bt_manager.cpp
│   │   │   ├── lora_manager.cpp
│   │   │   ├── pi_manager.cpp
│   │   │   ├── mtp_manager.cpp
│   │   │   ├── flasher.cpp
│   │   │   └── power_manager.cpp
│   │   └── assets/
│   │       ├── splash_cattopad.txt
│   │       └── splash_v3.txt
│   │
│   ├── system.meow/
│   │   ├── main.cpp
│   │   ├── manifest.json
│   │   └── assets/
│   │
│   ├── bridge.meow/
│   │   ├── main.cpp
│   │   ├── keymaps/
│   │   │   ├── cattopad_4x5.json
│   │   │   └── heltec_v3_2btn.json
│   │   └── manifest.json
│   │
│   └── explorer.meow/
│       ├── main.cpp
│       ├── manifest.json
│       └── assets/
│           └── icons/
│
├── apps/
│   ├── controlpanel.meow/
│   ├── notes.meow/
│   ├── calc.meow/
│   ├── keyboard.meow/
│   └── msn.meow/
│
├── apps/purr/
│   └── (user .purr binaries — drag and drop)
│
├── friends/
│   ├── friends.txt
│   ├── meshtastic.purr
│   └── bruce.purr
│
├── logs/
│   └── boot.txt              # Written on verbose boot or reserved key combo at startup
│
└── update/
    └── (firmware.bin or firmware.purr — dropped here for OTA)
```

---

## Communication Between Layers

All inter-layer communication is done via function calls to KITT's API or system.meow's API. There is no message bus or IPC middleware. The call hierarchy is strictly top-down:

```
explorer.meow
    └─ calls → system.meow (launch app, kill app, OTA)
    └─ calls → KITT (radio status, battery, app list, firmware list)

system.meow
    └─ calls → KITT (process exec, radio yield/reclaim, memory stats)
    └─ calls → bridge.meow (request handoff)

bridge.meow
    └─ calls → KITT (raw key events, radio yield/reclaim primitives)

KITT
    └─ owns hardware directly
    └─ calls back into explorer.meow via registered callback (tray update, popup)
```

KITT's callbacks into explorer.meow (registered at explorer startup):
```cpp
kitt.set_tray_update_cb(explorer_update_tray);
kitt.set_popup_cb(explorer_show_popup);
kitt.set_notify_cb(explorer_notify);
kitt.set_crash_report_cb(explorer_show_crash_report);
```

---

## Boot Verbose / Diagnostics

Controlled by `"verbose_boot": true/false` in device.json.

**Verbose ON:**
- KITT logs every step to serial + display (if large enough)
- Flag breakpoints pause boot on watched events — press any key to continue
- Small displays (SSD1306): log to serial only, jump to MTP recovery on flag

**Verbose OFF:**
- KITT renders ASCII boot splash from `boot_splash` file path in device.json
- No serial output (clean boot)

**Error codes on failure:**
- Small screens: `ERR:0x02` (single line, fits 16 chars)
- Large screens: `E_DISPLAY_INIT_FAIL` (full symbolic)

**On-device log retrieval:**
- Hold reserved combo at boot → KITT writes `/logs/boot.txt` → MTP mode
- If WiFi/BT/MTP unavailable and Meshtastic is in `/friends/`: KITT dumps log and hands to Meshtastic for LoRa transmission as last-resort telemetry

