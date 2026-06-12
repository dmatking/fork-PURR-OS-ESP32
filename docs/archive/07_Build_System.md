# Build System

---

## SDK wizard

The recommended way to configure and build. Run `python SDK/sdk_core.py --configure` (or the PowerShell wrapper on Windows).

```
python SDK/sdk_core.py [options]

  --configure           Full wizard (target + modules + ports)
  --build               Build firmware
  --flash               Flash to device
  --monitor             Open serial monitor
  --full-flash          Write partition table + factory + ota_0 (first flash)
  --clean               Clean build directory
```

The wizard saves choices to `SDK/purr_sdk.cfg` (JSON). Subsequent runs load it automatically. Use `--configure` to change settings.

---

## All CMake feature flags

These are set via the SDK wizard or passed directly with `-D` to `idf.py`.

### Target and display

| Flag | Values | Description |
|------|--------|-------------|
| `TARGET_DEVICE` | see table in [08_Devices.md](08_Devices.md) | Target hardware |
| `CYD_DISPLAY_VARIANT` | `s028r` / `s024c` | CYD touch variant |
| `PURR_UI_KERNEL` | `miniwin` / `none` | Window manager |
| `PURR_UI_THEME` | `wce` / `blackberry` | Shell theme (miniwin only) |

### Feature toggles

| Flag | Default | `PURR_HAS_*` define | Description |
|------|---------|---------------------|-------------|
| `PURR_ENABLE_BT` | 1 | `PURR_HAS_BT` | Bluetooth manager |
| `PURR_ENABLE_LORA` | auto | `PURR_HAS_LORA` | LoRa radio (auto-on for heltec/tdeck) |
| `PURR_ENABLE_MESH` | 0 | `PURR_HAS_MESH` | Meshtastic stack (requires LoRa) |
| `PURR_ENABLE_MTP` | 0 | `PURR_HAS_MTP` | MTP USB file transfer |
| `PURR_ENABLE_FLASHER` | 0 | `PURR_HAS_FLASHER` | OTA flasher |
| `PURR_ENABLE_LTE` | 0 | `PURR_HAS_LTE` | LTE modem (experimental) |
| `PURR_ENABLE_SHELL` | 1 | `PURR_HAS_SHELL` | USB serial REPL (drv_shell) |
| `PURR_ENABLE_LUA` | 1 | `PURR_HAS_LUA` | Lua 5.4 runtime |
| `PURR_ENABLE_MAGIDOS` | 0 | `PURR_HAS_MAGIDOS` | MagiDOS app (8086 emulator WIP) |
| `PURR_ENABLE_MAGICMAC` | 0 | `PURR_HAS_MAGICMAC` | MagicMac app (Mac Plus emulator WIP) |
| `BUILD_MINI` | 0 | — | Strip MicroPython (set 1 to save ~500KB) |

### Shell theme flags (set internally, not directly)

| Flag | Description |
|------|-------------|
| `PURR_THEME_BLACKBERRY` | Set when `PURR_UI_THEME=blackberry` |

---

## Module wizard prompts

When running `--configure`, the wizard for a CYD target shows:

```
  [1]  [*]  Bluetooth     ON   bt_manager
  [2]  [ ]  MTP USB       OFF  mtp_manager
  [3]  [ ]  OTA Flasher   OFF  flasher
  [4]  [ ]  LoRa Radio    OFF  (CYD has no LoRa hardware)
  [5]  [*]  Debug Shell   ON   drv_shell
  [6]  [*]  Lua Runtime   ON   lib_lua
  [7]  [ ]  MagiDOS       OFF  8086 emulator
  [8]  [ ]  MagicMac      OFF  Mac Plus emulator

        +-- ui: miniwin / theme: wce  ([u] kernel, [t] theme)

  Toggle [1-8], [u] UI kernel, [t] theme, or Enter to continue:
```

Press `[t]` to switch between `wce` and `blackberry`.

---

## Build directories

Each target gets its own isolated build directory so switching targets doesn't require a clean:

| Target | Build dir |
|--------|-----------|
| `cyd` | `CoreOS/build_cyd/` |
| `cyd_s028r` | `CoreOS/build_cyd_s028r/` |
| `cyd_s024c` | `CoreOS/build_cyd_s024c/` |
| `cyd_boot` | `CoreOS/build_cyd_boot/` |
| `heltec` | `CoreOS/build_heltec/` |
| `tembed_cc1101` | `CoreOS/build_tembed_cc1101/` |
| `tdeck_plus` | `CoreOS/build_tdeck_plus/` |
| `jc3248w535` | `CoreOS/build_jc3248/` |
| `waveshare169` | `CoreOS/build_waveshare/` |

sdkconfig is also per-target: `CoreOS/sdkconfig_cyd`, etc.

---

## Manual build example

> **Prefer `sdk_core.py` over `idf.py` directly.** The SDK wrapper sets the correct build directory, sdkconfig, and feature flags. Raw `idf.py` is for one-off experiments only.

```bash
# Recommended
python SDK/sdk_core.py --target cyd_s024c --build

# Or raw idf.py (advanced)
cd CoreOS
source ~/esp/idf/export.sh

idf.py \
  -DTARGET_DEVICE=cyd \
  -DPURR_UI_KERNEL=miniwin \
  -DPURR_UI_THEME=blackberry \
  -DPURR_ENABLE_LUA=1 \
  -DPURR_ENABLE_BT=0 \
  -DBUILD_MINI=1 \
  --build-dir build_cyd_s024c \
  set-target esp32 build

idf.py -p /dev/ttyUSB0 --build-dir build_cyd_s024c flash monitor
```

---

## Flash addresses (manual esptool)

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash \
  0x8000   build_cyd_s024c/partition_table/partition-table.bin \
  0x10000  build_cyd_boot/factory.bin \
  0x120000 build_cyd_s024c/purr_os.bin
```
