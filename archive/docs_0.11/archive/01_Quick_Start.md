# Quick Start

Build and flash PURR OS to a CYD board in five minutes.

---

## Prerequisites

- **ESP-IDF 5.3.5** installed and sourced (`export.sh` / `export.ps1`)
- **Python 3.8+** in PATH
- USB cable connected to your board

---

## Linux / macOS

```bash
# One-time setup
./SDK/setup_linux.sh          # installs ESP-IDF 5.3.5 if not present

# Interactive wizard — configure target, modules, port
python SDK/sdk_core.py --configure

# Build
python SDK/sdk_core.py --build

# Flash
python SDK/sdk_core.py --flash

# Monitor serial output
python SDK/sdk_core.py --monitor

# Build + flash in one step
python SDK/sdk_core.py --build --flash
```

## Windows (PowerShell)

```powershell
# Interactive wizard
.\SDK\SDK.ps1

# Quick build+flash
.\SDK\SDK.ps1 -Target cyd_s024c -Build -Flash COM8
```

---

## Manual idf.py (advanced)

```bash
cd CoreOS
idf.py -DTARGET_DEVICE=cyd -DPURR_UI_KERNEL=miniwin \
       -DPURR_ENABLE_LUA=1 -DPURR_UI_THEME=wce \
       set-target esp32 build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## First-time full flash (factory + ota_0)

Use when flashing a brand-new board or after a partition table change:

```bash
python SDK/sdk_core.py --full-flash
```

This writes the partition table, factory bootloader, and ota_0 OS image in one shot.

---

## Flash addresses (CYD)

| Region | Address | Size |
|--------|---------|------|
| Partition table | `0x8000` | 4 KB |
| Factory bootloader | `0x10000` | 1.0625 MB |
| ota_0 (OS) | `0x120000` | 1.4375 MB |
| ota_1 | `0x290000` | 1.0 MB |
| SPIFFS | `0x390000` | 448 KB |

---

## Putting scripts on the SD card

SD card must be formatted **FAT32**. PURR OS mounts it at `/sdcard`.

```
/sdcard/
  apps/
    hello.paws      ← userland Lua app
    admin.claw      ← admin Lua app (full KITT access)
```

Scripts appear automatically in the Apps launcher and Blackberry app drawer.

---

## Choosing a shell theme

In the SDK wizard, after selecting `ui_kernel = miniwin`, press `[t]` to pick:

| Theme | Description |
|-------|-------------|
| `wce` | Windows CE-style gray shell, Start menu, taskbar |
| `blackberry` | Green-on-black phosphor terminal, dock, swipe-up drawer |

Or pass directly: `-DPURR_UI_THEME=blackberry`
