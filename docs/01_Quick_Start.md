# PURR OS — Quick Start

## Prerequisites

- **ESP-IDF v5.3.5** installed at `~/esp/idf`  
  `IDF_PATH` must be set and `export.sh` sourced, or purrstrap will find it automatically.
- **Python 3.8+** with `pyserial` for port auto-detection (`pip install pyserial`)
- **esptool** (`pip install esptool`)

One-time IDF setup:
```bash
cd ~/esp/idf
./install.sh esp32,esp32s3
. ./export.sh
```

---

## Build your first firmware

```bash
git clone <repo>
cd PURR-OS-ESP32

# Configure (pick device, modules, ports)
./purrstrap.py init

# Build
./purrstrap.py build

# Flash
./purrstrap.py flash -p /dev/ttyUSB0

# Or build + flash in one step
./purrstrap.py install -p /dev/ttyUSB0

# Monitor serial output
./purrstrap.py monitor -p /dev/ttyUSB0
```

### Override device without reconfiguring

```bash
./purrstrap.py build tdeck_plus
./purrstrap.py flash tdeck_plus -p /dev/ttyAMC0
./purrstrap.py install cyd_s028r -p /dev/ttyUSB0
```

### Clean build

```bash
./purrstrap.py build --clean
./purrstrap.py build tdeck_plus --clean
```

---

## Flashing T-Deck Plus

The T-Deck Plus uses a USB-C port that shows up as `/dev/ttyAMC0` on Linux.

```bash
./purrstrap.py install tdeck_plus -p /dev/ttyAMC0
```

Full erase before first flash (recommended):
```bash
python -m esptool --chip esp32s3 -p /dev/ttyAMC0 erase_flash
./purrstrap.py flash tdeck_plus -p /dev/ttyAMC0
```

---

## Release builds (all devices)

```bash
# Build all devices + pack to baked/<device>/
./purrstrap.py release all

# Build a specific set
./purrstrap.py release miniwin      # CYD + JC + Waveshare + T-Deck Plus
./purrstrap.py release cyd          # CYD variants only

# Build + pack a single device
./purrstrap.py bake tdeck_plus
```

Each `baked/<device>/` folder contains:
- `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin`, `purr_os_core.bin`
- `spiffs.bin` (if applicable)
- `flash.sh` — ready-to-run esptool command
- `FLASH_GUIDE.md` — offsets, web flasher instructions, device notes

---

## purrstrap subcommands

| Command | Description |
|---------|-------------|
| `init` | Interactive wizard — pick device, modules, ports → `.purrstrap` |
| `status` | Show current config and build state |
| `list` | List all devices with build/bake status |
| `build [DEVICE]` | Build firmware |
| `flash [DEVICE] [-p PORT]` | Flash to device |
| `install [DEVICE] [-p PORT]` | Build + flash |
| `monitor [-p PORT]` | Serial monitor (Ctrl+] to exit) |
| `clean [DEVICE]` | Delete build dir |
| `bake [DEVICE\|SET\|all]` | Build + pack to baked/ |
| `release [SET]` | Batch release build |
| `scan` | Scan for connected serial devices |

---

## Module flags

Pass `--device` to override the configured device for a single command. All other flags are persistent in `.purrstrap` after `init`.

To enable optional modules at build time without going through `init`:
```bash
# Edit .purrstrap directly — it's plain JSON
cat .purrstrap
```

Key module keys: `wifi`, `bt`, `lora`, `mesh`, `gps`, `magidos`, `magicmac`, `lua`, `micropython`, `shell`

---

## Legacy SDK (deprecated)

`SDK/sdk_core.py` is still present but no longer maintained. Use `purrstrap.py` instead. The `SDK/targets/*.defaults` files are still used by purrstrap for sdkconfig defaults.
