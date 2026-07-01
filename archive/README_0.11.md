# PURR OS — v0.11.0

> **Disclaimer:** This project is very much vibe-coded. It has gone from 0 to "what" at an alarming rate. Fully open-source, contributions encouraged. Please.

**P.U.R.R.** = Portable Unified Runtime & Radio Operating System  
Powered by the **K.I.T.T** (Kernel Interface Translation Toolkit) kernel — a modular C++ embedded OS for ESP32, built on pure ESP-IDF 5.3.5.

---

## Quick Start

### Setup (one-time)

```bash
cd ~/esp/idf && ./install.sh esp32,esp32s3 && . ./export.sh
pip install pyserial esptool
```

### Build & Flash

```bash
./purrstrap.py init                            # wizard: pick device, modules, port
./purrstrap.py install -p /dev/ttyUSB0        # build + flash
./purrstrap.py monitor -p /dev/ttyUSB0        # serial monitor
```

### Common targets

```bash
./purrstrap.py install tdeck_plus -p /dev/ttyAMC0
./purrstrap.py install cyd_s028r  -p /dev/ttyUSB0
./purrstrap.py install heltec     -p /dev/ttyUSB0
./purrstrap.py list                            # show all devices + status
./purrstrap.py status                          # show current config
```

---

## Supported Devices

| Target | Chip | Display | Touch | Special |
|--------|------|---------|-------|---------|
| `tdeck_plus` | ESP32-S3 | ST7789 320×240 | GT911 cap | keyboard, trackball, SD, GPS |
| `jc3248w535` | ESP32-S3 | ST7796 480×320 | GT911 cap | 8MB PSRAM, MagiDOS |
| `cyd_s028r` | ESP32 | ILI9341 320×240 | XPT2046 resis | original CYD |
| `cyd_s024c` | ESP32 | ILI9341 240×320 | CST816S cap | newer CYD |
| `cyd_boot` | ESP32 | ILI9341 | CST816S | factory kernel |
| `heltec` | ESP32-S3 | SSD1306 OLED | — | SX1262 LoRa |
| `tdeck` | ESP32-S3 | ST7789 320×240 | — | trackball, LoRa (WIP) |
| `waveshare169` | ESP32-S3 | ST7789 240×280 | CST816S | WIP |
| `tembed_cc1101` | ESP32-S3 | ST7789 170×320 | — | CC1101 sub-GHz |

---

## Repo Layout

```
CoreOS/          base kernel (ESP-IDF project)
drivers/         hardware drivers: drv_display, drv_touch, drv_wifi, drv_bt,
                   drv_lora, drv_cc1101, drv_gps, drv_hid, drv_shell, drv_lte
ui/              UI frameworks: lib_miniwin, lib_tftespi, purr_wm
devices/         per-device HAL overlays (tdeck_plus, cyd, jc3248w535, ...)
  apps/          shared app source compiled into MiniWin WM
magidos/         MagiDOS 8086 DOS emulator (ESP32-S3 + 8MB PSRAM)
magicmac/        MagicMac Mac Plus emulator (WIP)
SDK/             sdkconfig defaults (targets/), setup_linux.sh
CattoHID/        CattoHID USB HID subproject
Userland/        .paw Lua app library
sdcard_apps/     example .paw scripts for SD card
docs/            documentation
  archive/       archived old docs and legacy materials
archive/         archived legacy build artifacts and old releases
purrstrap.py     build system CLI
```

---

## purrstrap.py

```
purrstrap init                  configure device → .purrstrap
purrstrap status                show current config
purrstrap list                  list devices
purrstrap build [DEVICE]        build firmware
purrstrap flash [DEVICE] [-p]   flash to device
purrstrap install [DEVICE] [-p] build + flash
purrstrap monitor [-p PORT]     serial monitor
purrstrap clean [DEVICE]        clean build dir
purrstrap bake [DEVICE|all]     build + pack to baked/
purrstrap release [SET]         batch release: all|miniwin|s3|cyd
purrstrap scan                  scan for serial ports
```

Config is stored in `.purrstrap` (gitignored). Build dirs are in `CoreOS/build_<device>/`. Release outputs go to `baked/<device>/` with a ready-to-run `flash.sh`.

---

## Versions

| Component | Version |
|-----------|---------|
| PURR OS   | v0.11.0 |
| KITT      | v0.8.0  |

---

## Documentation

- [docs/00_Overview.md](docs/00_Overview.md) — what it is, full layout
- [docs/01_Quick_Start.md](docs/01_Quick_Start.md) — setup + purrstrap usage
- [docs/02_Architecture.md](docs/02_Architecture.md) — boot sequence, HAL pattern, build system
- [docs/03_Devices.md](docs/03_Devices.md) — per-device pinouts and notes

---

## License

MIT. See individual subproject READMEs for their respective licenses (MiniWin, Lua 5.4, Meshtastic protobufs, BascomX 8086 emulator).
