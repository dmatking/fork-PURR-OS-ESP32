# PURR OS HID Edition — ESP32-S2 Spec (CattoBoardV1)

## Overview

PURR OS HID Edition is the minimal firmware that runs on the ESP32-S2-WROVER on
CattoBoardV1. Its primary job is scanning the 84-key matrix and sending keycodes
to the CM5 over USB HID. It also supports two lightweight extras: a minimal
handshake for OTA firmware updates to itself, and keymap config file drop-in so
key layouts can be changed without reflashing.

**Target chip:** ESP32-S2-WROVER (4MB flash, 2MB PSRAM)
**Runtime:** MicroPython
**USB role:** USB HID keyboard + minimal USB MSC (Mass Storage) for config drop-in

---

## What It Does

- Scans the 84-key matrix (14 columns × 6 rows)
- Sends USB HID keycodes to CM5
- Accepts OTA firmware updates via a minimal handshake
- Accepts keymap config file drop-in via USB Mass Storage

## What It Does NOT Do

- No WiFi
- No CDC serial channel
- No recovery triggers
- No watchdog of CM5
- No LoRa (LoRa is on CM5 via RAK3172)
- No data channel of any kind beyond update handshake

---

## Hardware Connections

| Pin group | Connected to |
|---|---|
| COLUMN0–13 | Keyboard matrix columns (14 GPIO) |
| ROW0–5 | Keyboard matrix rows (6 GPIO) |
| IO19/IO20 | Native USB D-/D+ → CM5 USB hub |
| Power/GND | 3.3V rail |

---

## USB Modes

The S2 presents two USB interfaces depending on state:

```
Normal operation:
  USB Interface 0: HID Keyboard  ← always active

Update / config mode (triggered by key combo at boot):
  USB Interface 0: HID Keyboard  ← still active
  USB Interface 1: USB MSC       ← exposes internal flash as a drive
                                    User drops keymap.json or firmware.bin
                                    S2 detects file, applies it, reboots
```

---

## OTA Update Handshake

Triggered by holding a reserved key combo (e.g. top-left + bottom-right corner
keys) while plugging in USB. S2 boots into update mode, presents as a USB drive.

```
User holds key combo → plug USB
      ↓
S2 boots into update mode
S2 enumerates as USB MSC (flash drive)
      ↓
User drops firmware.bin onto the drive
      ↓
S2 detects firmware.bin in root:
  - Validates file size (must be < 3MB)
  - Validates header (ESP32 magic byte 0xE9)
  - Writes to OTA partition
  - Deletes firmware.bin
  - Reboots into new firmware
      ↓
If validation fails:
  - Writes error.txt to drive explaining the failure
  - Does NOT flash
  - Reboots into existing firmware
```

### MicroPython implementation

```python
# purr_hid/ota.py
import os
import machine

FIRMWARE_FILE = "firmware.bin"
ERROR_FILE    = "error.txt"
ESP32_MAGIC   = 0xE9
MAX_SIZE      = 3 * 1024 * 1024  # 3MB

def check_and_apply_ota():
    if FIRMWARE_FILE not in os.listdir("/"):
        return False

    try:
        with open(FIRMWARE_FILE, "rb") as f:
            header = f.read(1)
            if header[0] != ESP32_MAGIC:
                _write_error("Invalid firmware: bad magic byte")
                return False

            f.seek(0, 2)  # Seek to end
            size = f.tell()
            if size > MAX_SIZE:
                _write_error(f"Firmware too large: {size} bytes (max {MAX_SIZE})")
                return False

        # Valid — write to OTA partition
        # (uses esp32 OTA API via MicroPython esp module)
        import esp
        with open(FIRMWARE_FILE, "rb") as f:
            esp.flash_write(esp.OTA_PARTITION_ADDR, f.read())

        os.remove(FIRMWARE_FILE)
        machine.reset()
        return True

    except Exception as e:
        _write_error(str(e))
        return False

def _write_error(msg):
    try:
        os.remove(FIRMWARE_FILE)
    except:
        pass
    with open(ERROR_FILE, "w") as f:
        f.write(f"OTA Error: {msg}\n")
```

---

## Keymap Config Drop-In

User drops a `keymap.json` file onto the USB drive (in update mode or via
a dedicated keymap-swap mode triggered by a different key combo). S2 detects
it, validates it, replaces the active keymap, and reboots.

No OTA needed for keymap changes — keymap lives in flash as a JSON file,
not baked into the firmware.

```python
# purr_hid/keymap_loader.py
import os
import json

KEYMAP_FILE   = "keymap.json"
ACTIVE_KEYMAP = "/flash/keymap_active.json"

def check_and_apply_keymap():
    if KEYMAP_FILE not in os.listdir("/"):
        return False

    try:
        with open(KEYMAP_FILE, "r") as f:
            data = json.load(f)

        # Validate — must have at least one row,col entry
        if not isinstance(data, dict) or len(data) == 0:
            _write_error("keymap.json is empty or invalid")
            return False

        # Check all keys are row,col format
        for k in data.keys():
            parts = k.split(",")
            if len(parts) != 2:
                _write_error(f"Bad keymap entry: {k} (expected row,col)")
                return False

        # Write to active keymap slot
        with open(ACTIVE_KEYMAP, "w") as f:
            json.dump(data, f)

        os.remove(KEYMAP_FILE)
        import machine
        machine.reset()
        return True

    except Exception as e:
        _write_error(str(e))
        return False

def load_active_keymap():
    try:
        with open(ACTIVE_KEYMAP, "r") as f:
            return json.load(f)
    except:
        # Fall back to built-in default
        with open("/flash/keymap_default.json", "r") as f:
            return json.load(f)

def _write_error(msg):
    with open("keymap_error.txt", "w") as f:
        f.write(f"Keymap Error: {msg}\n")
```

---

## keymap_default.json

Maps `row,col` matrix position to HID keycode string.
Full layout filled in once physical Ingenico Move 5000 key
positions are confirmed against the schematic matrix wiring.

```json
{
  "0,0": "ONE",   "0,1": "TWO",   "0,2": "THREE",
  "0,3": "FOUR",  "0,4": "FIVE",  "0,5": "SIX",
  "0,6": "SEVEN", "0,7": "EIGHT", "0,8": "NINE",
  "0,9": "ZERO",
  "1,0": "Q", "1,1": "W", "1,2": "E", "1,3": "R",
  "1,4": "T", "1,5": "Y", "1,6": "U", "1,7": "I",
  "1,8": "O", "1,9": "P",
  "2,0": "A", "2,1": "S", "2,2": "D", "2,3": "F",
  "2,4": "G", "2,5": "H", "2,6": "J", "2,7": "K",
  "2,8": "L",
  "3,0": "Z", "3,1": "X", "3,2": "C", "3,3": "V",
  "3,4": "B", "3,5": "N", "3,6": "M",
  "4,0": "SPACE",
  "4,1": "RETURN",
  "4,2": "BACKSPACE",
  "5,0": "ESCAPE"
}
```

---

## main.py — Boot Sequence

```python
# main.py
import usb_hid
import time
import json
from machine import Pin
from adafruit_hid.keyboard import Keyboard
from adafruit_hid.keycode import Keycode
from ota import check_and_apply_ota
from keymap_loader import check_and_apply_keymap, load_active_keymap

# Check update mode key combo before USB init
UPDATE_TRIGGER = (Pin(0, Pin.IN, Pin.PULL_UP),
                  Pin(13, Pin.IN, Pin.PULL_UP))

update_mode = all(not p.value() for p in UPDATE_TRIGGER)

if update_mode:
    # Mount flash as USB MSC drive
    import storage
    storage.remount("/", readonly=False)
    # USB MSC presented to host — wait for file drop
    while True:
        check_and_apply_ota()
        check_and_apply_keymap()
        time.sleep(1)
else:
    # Normal HID mode
    usb_hid.enable((usb_hid.Device.KEYBOARD,))
    kbd = Keyboard(usb_hid.devices)
    keymap = load_active_keymap()

    COLS = [0,1,2,3,4,5,6,7,8,9,10,11,12,13]
    ROWS = [14,15,16,17,18,21]

    col_pins = [Pin(p, Pin.OUT, value=1) for p in COLS]
    row_pins = [Pin(p, Pin.IN, Pin.PULL_UP) for p in ROWS]
    prev = [[False]*len(COLS) for _ in range(len(ROWS))]

    while True:
        for c, col in enumerate(col_pins):
            col.value(0)
            time.sleep_us(10)
            for r, row in enumerate(row_pins):
                pressed = not row.value()
                if pressed != prev[r][c]:
                    key = keymap.get(f"{r},{c}")
                    if key:
                        kc = getattr(Keycode, key, None)
                        if kc:
                            if pressed: kbd.press(kc)
                            else:        kbd.release(kc)
                    prev[r][c] = pressed
            col.value(1)
        time.sleep_ms(5)
```

---

## Summary

| Feature | Included | Notes |
|---|---|---|
| USB HID keyboard | Yes | Primary job |
| OTA firmware update | Yes | Key combo + USB drive drop-in |
| Keymap config swap | Yes | JSON file drop-in, no reflash needed |
| WiFi | No | Not wired, not needed |
| CDC serial | No | Not needed |
| Recovery triggers | No | Not needed |
| CM5 handshake | No | CM5 sees S2 as a plain USB keyboard |
| LoRa | No | CM5 owns LoRa via RAK3172 |
