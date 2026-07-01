# PURR OS — Overview

**P.U.R.R.** = Portable Unified Runtime & Radio Operating System  
Powered by the **K.I.T.T** (Kernel Interface Translation Toolkit) kernel — a modular C++ embedded OS for ESP32 hardware, built on pure ESP-IDF 5.3.5.

---

## What is PURR OS?

PURR OS is a full embedded operating system for ESP32 devices with:

- **MiniWin WM** — a window manager with touch, mouse cursor, keyboard navigation
- **KittenUI** — a text-mode shell for small/headless displays (OLED, T-Deck)
- **Kernel modules** — WiFi, Bluetooth, LoRa, GPS, SD card, power management
- **App runtime** — Lua 5.4 scripting, MicroPython, PDL user-loaded drivers
- **MagiDOS** — 8086 DOS emulator (requires 8MB PSRAM)
- **MagicMac** — Mac Plus emulator (requires 8MB PSRAM)

---

## Supported Devices

| Target | Chip | Display | Touch | Notes |
|--------|------|---------|-------|-------|
| `tdeck_plus` | ESP32-S3 | ST7789 320×240 | GT911 cap | keyboard, trackball, SD, GPS opt |
| `jc3248w535` | ESP32-S3 | ST7796 480×320 | GT911 cap | 8MB PSRAM, SD |
| `cyd_s028r` | ESP32 | ILI9341 320×240 | XPT2046 resis | original CYD |
| `cyd_s024c` | ESP32 | ILI9341 240×320 | CST816S cap | newer CYD |
| `cyd_boot` | ESP32 | ILI9341 | CST816S | factory kernel only |
| `heltec` | ESP32-S3 | SSD1306 OLED | — | SX1262 LoRa |
| `tdeck` | ESP32-S3 | ST7789 320×240 | — | trackball, SX1262 LoRa (WIP) |
| `waveshare169` | ESP32-S3 | ST7789 240×280 | CST816S cap | WIP |
| `tembed_cc1101` | ESP32-S3 | ST7789 170×320 | — | CC1101 sub-GHz, rotary encoder |

---

## Repository Layout

```
PURR-OS-ESP32/
  CoreOS/          base kernel (ESP-IDF project root)
    main/          CMakeLists.txt — device selection + module flags
    system/        kernel/, bridge/, system/, micropython/
    components/    lib_lua, lib_arduino, lib_nanopb, lib_radiolib, lib_mesh_pb
    sdkconfig_*    per-device sdkconfig cache
    build_*/       per-device build dirs (gitignored)

  drivers/         hardware drivers (ESP-IDF components)
    drv_display/   ILI9341, ST7789, ST7796, SSD1306
    drv_touch/     GT911, XPT2046, CST816S, MXT336T
    drv_wifi/      WiFi manager
    drv_bt/        Bluetooth manager
    drv_lora/      LoRa radio (SX1262, SX1276, RAK3172 kernels)
    drv_cc1101/    CC1101 sub-GHz radio
    drv_gps/       GPS manager (u-blox MIA-M10Q)
    drv_hid/       keyboard matrix, USB HID
    drv_shell/     USB serial debug REPL
    drv_lte/       LTE manager (experimental)

  ui/              UI frameworks (ESP-IDF components)
    lib_miniwin/   MiniWin WM + device HAL bridge
    lib_tftespi/   TFT_eSPI display backend
    purr_wm/       PURR WM shell adapter

  devices/         per-device HAL overlays
    tdeck_plus/    hal_lcd, hal_touch, hal_input, purr_app, miniwin_config
    cyd/           hal_*, purr_app, miniwin_config (wce/blackberry/luna themes)
    cyd_s028r/     hal_*, purr_app, miniwin_config
    cyd_s024c/     hal_*, purr_app, miniwin_config
    jc3248w535/    hal_*, purr_app, miniwin_config
    waveshare169/  hal_*, purr_app, miniwin_config
    heltec/        hal stubs
    tdeck/         hal stubs
    tembed_cc1101/ hal stubs
    generic/       fallback stubs
    apps/          shared app source compiled into the WM

  baked/           release outputs — flash.sh + bins per device (gitignored)
  purrstrap.py     build system CLI (replaces SDK/sdk_core.py)
  SDK/             legacy; targets/*.defaults still used by purrstrap
  magidos/         MagiDOS 8086 emulator subproject
```

---

## Quick reference

```bash
./purrstrap.py list                      # show all devices + build status
./purrstrap.py init                      # configure device wizard
./purrstrap.py build tdeck_plus          # build
./purrstrap.py install tdeck_plus -p /dev/ttyAMC0  # build + flash
./purrstrap.py bake all                  # build + pack all devices to baked/
```

See [01_Quick_Start.md](01_Quick_Start.md) for full setup instructions.
