# PURR OS — Flashing Guide

Covers every method to flash PURR OS: command-line esptool, purrstrap, and browser-based web flasher. All methods write the same four binary files.

---

## Files needed

Every PURR OS release packages four files per device, found in `baked/<device>/`:

| File | Offset | Description |
|------|--------|-------------|
| `bootloader.bin` | `0x1000` | ESP-IDF second-stage bootloader |
| `partition-table.bin` | `0x8000` | Partition layout |
| `purr_os_core.bin` | `0x10000` | PURR OS firmware |
| `spiffs.bin` | `0x390000` (CYD) / `0x3b0000` (other) | SPIFFS filesystem (fonts, config, Lua) |

> **S3 devices** (T-Deck Plus, JC3248W535, Heltec, etc.) have their bootloader at `0x0`, not `0x1000`.

---

## Method 1 — purrstrap (recommended for development)

```bash
# Build + flash in one step
python3 purrstrap.py install cyd_s028r -p /dev/ttyUSB0

# Flash a pre-built firmware
python3 purrstrap.py flash cyd_s028r -p /dev/ttyUSB0

# Auto-detect port
python3 purrstrap.py flash cyd_s028r
```

Always run from the repo root. purrstrap handles chip type, flash mode, and offsets automatically.

---

## Method 2 — esptool (command line)

### Install

```bash
pip install esptool
```

### ESP32 devices (CYD, T-Deck)

```bash
python -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000    bootloader.bin \
  0x8000    partition-table.bin \
  0x10000   purr_os_core.bin \
  0x390000  spiffs.bin
```

### ESP32-S3 devices (T-Deck Plus, JC3248W535, Heltec)

```bash
python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0       bootloader.bin \
  0x8000    partition-table.bin \
  0x10000   purr_os_core.bin \
  0x3b0000  spiffs.bin
```

### Using the bundled flash.sh

Each `baked/<device>/` contains a ready-to-run script:

```bash
cd baked/cyd_s028r
./flash.sh /dev/ttyUSB0
```

### Full erase before first flash (recommended)

Clears NVS (stored calibration, Wi-Fi credentials, settings) and ensures a clean state:

```bash
# ESP32
python -m esptool --chip esp32 -p /dev/ttyUSB0 erase_flash

# ESP32-S3
python -m esptool --chip esp32s3 -p /dev/ttyACM0 erase_flash
```

---

## Method 3 — ESP Web Flasher (browser, no drivers needed)

The ESP Web Flasher runs entirely in the browser using the Web Serial API. No drivers, no Python, no command line required. Works on Windows, macOS, and Linux with Chrome or Edge.

**Open:** `https://espressif.github.io/esptool-js/`

### Steps

1. Click **Connect** and select your device's serial port from the browser dialog.
2. Set **Baud Rate** to `460800`.
3. Click **Erase Flash** if doing a first-time install (optional but recommended).
4. Under **Flash**, add each file one at a time using **Add File**:

   For **ESP32 devices** (CYD variants):

   | Offset | File |
   |--------|------|
   | `0x1000` | `bootloader.bin` |
   | `0x8000` | `partition-table.bin` |
   | `0x10000` | `purr_os_core.bin` |
   | `0x390000` | `spiffs.bin` |

   For **ESP32-S3 devices** (T-Deck Plus, JC3248W535, Heltec):

   | Offset | File |
   |--------|------|
   | `0x0` | `bootloader.bin` |
   | `0x8000` | `partition-table.bin` |
   | `0x10000` | `purr_os_core.bin` |
   | `0x3b0000` | `spiffs.bin` |

5. Click **Program** and wait for all four files to complete.
6. Press the **Reset** button on your device (or power cycle).

### Troubleshooting web flasher

| Issue | Fix |
|-------|-----|
| Port not listed | Use Chrome or Edge (Firefox doesn't support Web Serial). Enable Web Serial at `chrome://flags` if needed. |
| "Failed to open serial port" | Close any other apps using the port (serial monitors, purrstrap monitor). |
| T-Deck Plus not detected | Hold **Fn** while pressing **Reset** to enter download mode, then connect. |
| CYD not detected | Hold **BOOT** button while connecting USB, then release after connection. |
| Flash fails mid-way | Lower baud to `115200` and retry. |

---

## Port names by OS

| OS | ESP32 | ESP32-S3 |
|----|-------|---------|
| Linux | `/dev/ttyUSB0` | `/dev/ttyACM0` |
| macOS | `/dev/cu.usbserial-*` | `/dev/cu.usbmodem*` |
| Windows | `COM3` (or higher) | `COM3` (or higher) |

On Linux, add yourself to the `dialout` group if you get permission errors:
```bash
sudo usermod -aG dialout $USER
# Log out and back in for this to take effect
```

---

## Per-device notes

### CYD S028R (`cyd_s028r`)
- **Boot mode:** Hold **BOOT** button while connecting USB to enter download mode.
- **Touch calibration:** XPT2046 resistive touch runs a 3-point calibration on first boot. Tap each crosshair precisely. Calibration is stored in NVS — erase flash to recalibrate.
- **SD card:** Insert before power-on. Formatted FAT32.

### CYD S024C (`cyd_s024c`)
- Same boot mode as S028R.
- CST816S capacitive touch — no calibration needed.
- **Backlight pin is GPIO 27**, not GPIO 21 (different from S028R).

### T-Deck Plus (`tdeck_plus`)
- **Boot mode:** Hold **Fn** + **Reset** to enter download mode.
- **Port:** Shows as `/dev/ttyACM0` on Linux.
- **Power:** GPIO 10 must be driven HIGH on boot — handled by firmware, not a user concern.
- Touch calibration runs on first boot (GT911 capacitive, 3 points).

### Heltec WiFi LoRa 32 V3 (`heltec`)
- **Boot mode:** Hold **PRG** button while connecting.
- LoRa requires an antenna connected before transmitting. Do not transmit without an antenna.

---

## Verifying a flash

After flashing, open the serial monitor at 115200 baud:

```bash
python3 purrstrap.py monitor -p /dev/ttyUSB0
# or
python -m serial.tools.miniterm /dev/ttyUSB0 115200
```

A successful boot prints:
```
I (xxx) kitt: PURR OS v0.11.0  KITT v0.8.0  boot start
...
I (xxx) KITT: ready
```

---

## OTA (firmware update over the air)

PURR OS supports OTA updates via the partition manager. Place a `purr_os_core.bin` on an SD card and use the **Firmware** option in the Settings app to flash it to an OTA slot. The device reboots into the new firmware; if it fails to boot, it rolls back to the previous slot automatically.
