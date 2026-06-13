# PURR OS — v0.12.1

**P.U.R.R.** = Portable Unified Runtime & Radio Operating System
**K.I.T.T** = Kernel Interface Translation Toolkit

A fully modular, plug-and-play embedded OS for ESP32/ESP32-S3 devices, built on ESP-IDF.
Inspired by QubesOS — every driver, UI framework, and app is an isolated, precompiled module.
The kernel spine knows nothing about hardware; it loads modules and hands off.

---

## Architecture

```
Kernel spine  →  loads .purr modules at boot
                  driver_manager  →  registers catcalls (display, touch, input, radio, gps, ui)
                  kittenui / miniwin  →  registers catcall_ui_t (unified widget layer)
                  app_manager  →  launches .meow / .paws / .claw apps
                                   purr_win.h dispatch
                                        ↓
                                   catcall_ui_t backend (LVGL or MiniWin)
                                        ↓
                                   catcall_display_t.push_pixels()
                                        ↓
                                   physical display
```

### Extension types

| Extension | What it is |
|-----------|-----------|
| `.purr` | Precompiled kernel module (driver, system service, UI framework) |
| `.meow` | Lua 5.4 script — sandboxed VM, `win.*` / `sd.*` / `kitt.*` API |
| `.paws` | Compiled userland app — `purr_win.h` + `sd.*` only |
| `.claw` | Compiled kernel-access app — full `purr_kernel_*` + `purr_win.h` |

### Catcalls

The kernel's hardware abstraction layer — named "catcalls" (PURR OS version of syscalls).
Drivers register implementations; everything else calls through the kernel accessor. Current catcalls:

| Catcall | Purpose | Accessor |
|---------|---------|---------|
| `display` | Pixel output (push_pixels, fill_rect, brightness) | `purr_kernel_display()` |
| `touch` | Touch point reading | `purr_kernel_touch()` |
| `input` | Keyboard/trackball HID events | `purr_kernel_input()` |
| `radio` | LoRa SPI radio (send/receive/RSSI/SNR) | `purr_kernel_radio()` |
| `gps` | NMEA UART GPS fix | `purr_kernel_gps()` |
| `ui` | Widget/window layer (new in v0.12.0) | `purr_kernel_ui()` |

### Unified UI API

Apps never call LVGL or MiniWin directly. All UI goes through `purr_win.h`:

```c
#include "purr_win.h"

purr_win_t win = purr_win_create("My App");
purr_wid_t lbl = purr_win_label(win, "Hello PURR OS!");
purr_win_button(win, "Tap", on_tap, NULL);
purr_win_show(win);
```

This compiles once and runs on KittenUI (LVGL), MiniWin, and any future UI backend.

---

## Repo Layout

```
source/
  kernel/
    catcalls/           catcall_display.h, touch, input, radio, gps, ui; purr_win.h
    core/               boot.c, purr_kernel.h/.c, purr_module.h (.purr ABI)
  drivers/              display/, touch/, input/, radio/, gps/
  modules/
    driver_manager/     scans + loads .purr driver blobs
    app_manager/        launches .meow/.paws/.claw apps
    kittenui/           LVGL UI module + catcall_ui_t backend
    miniwin/            MiniWin WM module + catcall_ui_t backend
    oled_ui/            Text-mode OLED UI for 128x64 displays
  devices/              device.pcat manifests (8 devices)
  apps/
    system/             settings, about, terminal, fileman, calculator
    exclusive/          magicmac, magidos (rewrite in progress)

purrstrap/              builds final flashable firmware image
modulestrap/            compiles .purr kernel module + driver blobs
catstrap/               user app builder + SDK (.meow/.paws/.claw)
user_drivers/           drop custom/community drivers here — auto-scanned

cattobaked/             all build output
  <device>/             purrstrap output (firmware.bin, flash.bin, glue/, ...)
  modules/              modulestrap output (.purr system module blobs)
  drivers/              modulestrap output (.purr driver blobs by type)
  apps/                 catstrap output (.claw/.paws/.meow + .meta.json)

docs/                   documentation (13 files)
PURR-OS-0.11/           archived v0.11 codebase
archive/                legacy scripts, old docs, old baked artifacts
```

---

## Supported Devices

| Device | Chip | Screen | Radio | SD | Apps |
|--------|------|--------|-------|----|------|
| `jc3248w535` | ESP32-S3 | 3.5" AXS15231B 480x320 QSPI | WiFi + BT | no | all 5 |
| `tdeck_plus` | ESP32-S3 | 3.2" ST7789 320x240 | WiFi + BT + SX1276 LoRa + GPS | yes | all 5 |
| `tdeck` | ESP32-S3 | 3.2" ST7789 320x240 | WiFi + BT + SX1262 LoRa | yes | all 5 |
| `cyd` | ESP32 | 2.8" ILI9341 320x240 | WiFi + BT | yes | all 5 |
| `cyd_s024c` | ESP32 | 2.4" ILI9341 240x320 | WiFi + BT | yes | all 5 |
| `cyd_s028r` | ESP32 | 2.8" ILI9341 320x240 | WiFi + BT | yes | all 5 |
| `heltec` | ESP32-S3 | 128x64 SSD1306 OLED | WiFi + BT + SX1262 LoRa | no | none |
| `waveshare169` | ESP32-S3 | 1.69" ST7789 240x280 | WiFi + BT | no | none |

---

## Build Tools

All commands run from the repo root. Use `python3 <tool>.py --help` for full options.

### purrstrap — final image builder

```
purrstrap build <device>         build firmware (reads source/devices/<device>/device.pcat)
purrstrap flash <device> [-p P]  build + flash to connected device
purrstrap clean <device>         remove build artifacts
purrstrap list                   list supported devices with radio capabilities
purrstrap status                 show workspace config
purrstrap doctor                 check environment (IDF, Python, source tree)
```

`purrstrap build` automatically invokes modulestrap and catstrap — you do not need to run them separately.

### modulestrap — .purr module + driver compiler

```
modulestrap build all            register all modules and drivers
modulestrap build <name>         register one target (e.g. "kittenui", "display/ili9341")
modulestrap list                 list all buildable targets
modulestrap clean [all]          remove .purr blobs from cattobaked/
```

Custom drivers in `user_drivers/` are picked up automatically.

### catstrap — user app builder + SDK

```
catstrap build all               build/register all apps + MagicMac + MagiDOS
catstrap build <name>            build/register one app
catstrap validate <file.meow>    syntax-check a Lua script
catstrap sdk install             generate SDK headers to catstrap/sdk/include/
catstrap sdk info                show SDK version and API surface
catstrap list                    list all apps
catstrap clean [all]             remove app output from cattobaked/apps/
```

---

## System Apps

Five apps are bundled on all medium/large-screen devices:

| App | Tier | Description |
|-----|------|-------------|
| `settings` | `.claw` | Theme, brightness, SD status, reboot |
| `about` | `.claw` | OS/KITT version, chip info, free RAM, uptime, active drivers |
| `terminal` | `.claw` | Shell: ls, cat, echo, modules, mem, uptime, reboot |
| `fileman` | `.claw` | Browse SPIFFS + SD card; text file preview |
| `calculator` | `.paws` | Basic arithmetic with decimal support |

All apps use `purr_win.h` — they work on any registered UI backend without changes.

---

## Versions

| Component | Version |
|-----------|---------|
| PURR OS | v0.12.1 |
| KITT | v0.9.1 |
| .purr ABI | 1 |
| Catcall API | 1 (+ catcall_ui as slot 6) |

---

## Documentation

| Doc | Contents |
|-----|---------|
| [docs/00_Overview.md](docs/00_Overview.md) | What PURR OS is, supported hardware, key concepts |
| [docs/01_Architecture.md](docs/01_Architecture.md) | Kernel spine, module loader, .purr ABI, catcall registry |
| [docs/02_Catcalls.md](docs/02_Catcalls.md) | All six catcalls with full struct + function docs, driver tables, glue layer |
| [docs/03_Modules.md](docs/03_Modules.md) | driver_manager, app_manager, kittenui, miniwin, oled_ui |
| [docs/04_Devices.md](docs/04_Devices.md) | device.pcat format, all 8 devices, pin reference, screen classification |
| [docs/05_Drivers.md](docs/05_Drivers.md) | driver.pcat format, all drivers, writing a new driver |
| [docs/06_Apps.md](docs/06_Apps.md) | Tiers, purr_win.h API reference, .meow/.paws/.claw guide, system apps |
| [docs/07_Build_Tools.md](docs/07_Build_Tools.md) | purrstrap, modulestrap, catstrap — full pipeline and command reference |
| [docs/08_Exclusives.md](docs/08_Exclusives.md) | MagicMac and MagiDOS — architecture, IPC, build instructions, status |
| [docs/12_AppAPI.md](docs/12_AppAPI.md) | purr_win.h unified API — complete reference, backend writing guide |

---

## Prior versions

The v0.11.0 codebase is preserved in `PURR-OS-0.11/`.
Legacy docs, changelogs, and build artifacts are in `archive/`.

---

## License

MIT. MiniWin: MIT (John Blaiklock). Lua 5.4: MIT. See subproject READMEs for full attribution.
