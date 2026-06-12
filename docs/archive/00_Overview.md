# PURR OS — Overview

**PURR OS v0.10.1 / KITT v0.7.0**

> P.U.R.R. = Portable Unified Runtime & Radio Operating System

PURR OS is a modular embedded operating system for ESP32 devices. It runs on cheap $10–$30 LCD boards, provides a full windowed UI via the **MiniWin** window manager, executes **Lua scripts** from an SD card, and optionally manages LoRa radio, Bluetooth, and OTA firmware updates.

---

## Hardware targets

| Target ID | Board | Display | Touch | Notes |
|-----------|-------|---------|-------|-------|
| `cyd_s024c` | ESP32-2432S024C | ILI9341 320×240 | CST816S I2C | Recommended CYD target |
| `cyd_s028r` | ESP32-2432S028R | ILI9341 320×240 | XPT2046 SPI | 8-sample avg, edge-drift fixed |
| `cyd_boot` | Any CYD | ILI9341 | CST816S | Factory bootloader only |
| `tdeck_plus` | LilyGo T-Deck Plus | ST7789 320×240 | GT911 cap | Keyboard, trackball, GPS, LoRa optional |
| `tdeck` | LilyGo T-Deck (original) | ST7789 320×240 | — | LoRa enabled |
| `jc3248w535` | JC3248W535 3.5″ | ST7796 480×320 | GT911 I2C | 8MB PSRAM |
| `waveshare169` | Waveshare 1.69″ | ST7789 240×280 | CST816S | WIP |
| `heltec` | Heltec WiFi LoRa 32 V3 | — (KittenUI) | — | Headless / LoRa SX1262 |
| `tembed_cc1101` | LilyGo T-Embed CC1101 | ST7789 170×320 | — | Rotary encoder / CC1101 |

---

## System layers

```
┌─────────────────────────────────────────────┐
│  Lua scripts (.paws / .claw)  SD card apps  │  User scripts
├─────────────────────────────────────────────┤
│  MiniWin app windows  (devices/apps/)       │  App layer
│  WCE shell / Blackberry shell               │  UI shell
├─────────────────────────────────────────────┤
│  MiniWin WM  (lib_miniwin)                  │  Window manager
├─────────────────────────────────────────────┤
│  PDL drivers (.drv)  /sdcard/drvdebug/      │  Hot-loaded drivers
├─────────────────────────────────────────────┤
│  KITT kernel  v0.7.0                        │  Kernel services
│  WiFi · BT · LoRa · SD · Power · OTA       │
├─────────────────────────────────────────────┤
│  ESP-IDF 5.3.5  (pure, no Arduino)          │  Platform
└─────────────────────────────────────────────┘
```

---

## Key concepts

- **KITT** — the kernel singleton (`extern KITT kitt`). Owns all hardware subsystems. See [03_KITT_API.md](03_KITT_API.md).
- **MiniWin** — embedded window manager. Each app is a `mw_handle_t` window with paint + message callbacks. See [04_MiniWin_Shell.md](04_MiniWin_Shell.md).
- **App catalog** — static list of built-in apps (`purr_catalog[]`). Shell Start menu and Blackberry drawer both read from it.
- **Lua scripts** — `.paws` (sandboxed) and `.claw` (admin) scripts on `/sdcard/apps/`. Launched via `app_lua_window_create()`. See [05_Lua_Scripting.md](05_Lua_Scripting.md).
- **PDL drivers** — C-like `.drv` scripts hot-loaded from `/sdcard/drvdebug/` at runtime via `drvmgr`. See [11_PDL_Drivers.md](11_PDL_Drivers.md).
- **SDK** — Python wizard (`SDK/sdk_core.py`) for configuring, building, and flashing. See [07_Build_System.md](07_Build_System.md).

---

## Docs index

| File | Contents |
|------|----------|
| [01_Quick_Start.md](01_Quick_Start.md) | Build, flash, monitor in 5 minutes |
| [02_Architecture.md](02_Architecture.md) | Component graph, boot sequence, partition layout |
| [03_KITT_API.md](03_KITT_API.md) | Full KITT C++ kernel API reference |
| [04_MiniWin_Shell.md](04_MiniWin_Shell.md) | Shell themes, window system, taskbar |
| [05_Lua_Scripting.md](05_Lua_Scripting.md) | Writing .paws/.claw scripts, win.* / sd.* / kitt.* API |
| [06_App_Development.md](06_App_Development.md) | Writing MiniWin apps in C++ |
| [07_Build_System.md](07_Build_System.md) | CMake flags, SDK wizard, all feature toggles |
| [08_Devices.md](08_Devices.md) | Hardware targets, pin tables, device JSON |
| [09_Partition_Manager.md](09_Partition_Manager.md) | OTA slots, SD firmware install, pm_* API |
| [10_MagicMac_System.md](10_MagicMac_System.md) | MagicMac Plus emulator internals |
| [11_PDL_Drivers.md](11_PDL_Drivers.md) | Writing `.drv` scripts, PDL language reference, drvmgr shell |
