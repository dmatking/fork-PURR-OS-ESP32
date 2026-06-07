# CattoBoardV1 — System Architecture Specification

## Overview

CattoBoardV1 is the first hardware revision of the CattoPad cyberdeck, based on the
Ingenico Move 5000 form factor (169×78×57mm). It is a dual-compute platform — a
Raspberry Pi CM5 handles the primary OS and heavy compute, while an ESP32-S2-WROVER
acts as a permanent co-processor handling input, WiFi, and Pi control. PURR OS runs
on the CM5. PURR-lite (MicroPython) runs on the ESP32-S2.

---

## Physical Form Factor

Based on the Ingenico Move 5000 payment terminal:
- **Dimensions:** 169 × 78 × 57mm
- **Display:** 3.5" ILI9488 320×480 touchscreen (mXT336T capacitive touch)
- **Input:** 84-key keyboard matrix (Ingenico Move 5000 keypad layout)
- **Battery:** 3.6V 2900mAh Li-ion (Ingenico OEM, 3-pin JST)

---

## Compute Architecture

```
┌─────────────────────────────────────────────┐
│               CM5 (Primary)                  │
│  Raspberry Pi Compute Module 5               │
│  Runs: PURR OS (full stack)                  │
│  Owns: Display, Touch, LoRa UART, HDMI out  │
│  USB: Receives HID + CDC from ESP32-S2       │
└──────────────────┬──────────────────────────┘
                   │ USB (HID + CDC composite)
┌──────────────────┴──────────────────────────┐
│           ESP32-S2-WROVER (Co-processor)     │
│  Runs: PURR-lite (MicroPython)               │
│  Owns: 84-key matrix, WiFi agent            │
│  USB: HID keyboard + CDC control channel    │
└─────────────────────────────────────────────┘
```

---

## Board Components (from schematic)

| Component | Part | Role |
|---|---|---|
| CM5 | Raspberry Pi CM5 | Primary compute — PURR OS |
| ESP32-S2-WROVER | U1 | Co-processor — keyboard HID + WiFi + Pi control |
| RAK3172 | U502 (STM32WLE5) | LoRa module — UART to CM5 |
| ILI9488 | Display | 3.5" 320×480 touchscreen |
| mXT336T | Touch controller | Capacitive touch — I2C to CM5 |
| SHTC3 | U601 | Temperature/humidity sensor — I2C |
| AP22653W6 | U701 | USB current-limit switch |
| J101 | USB-C | Power input only |
| USB701 | USB-C | USB 2.0 hub output to CM5 |
| J601 | FAN connector | PWM + TACHO fan control |
| J502 | I2C header | Expansion port |
| SW601 | B3U-3000P | Reset button |
| SW602 | B3U-3000P | Power button |

---

## ESP32-S2-WROVER — PURR-lite Co-processor

### Hardware constraints (as wired on CattoBoardV1)

| Pin group | What's connected |
|---|---|
| COLUMN0–13 | Keyboard matrix columns (14 lines) |
| ROW0–5 | Keyboard matrix rows (6 lines) |
| IO19/IO20 | Native USB D-/D+ → CM5 USB hub |
| Power/GND | 3.3V rail |
| WiFi antenna | On-module PCB antenna |

**Not wired:** display, LoRa, I2C, UART to CM5, reset/power GPIO.
**Chip limit:** WiFi only — no Bluetooth on ESP32-S2.

### Memory

| Resource | Total | HID firmware | MicroPython | PURR-lite scripts | Free |
|---|---|---|---|---|---|
| Flash | 4MB | ~50KB | ~1.2MB | ~500KB | ~2.25MB |
| PSRAM | 2MB | ~10KB | ~150KB | ~100KB | ~1.75MB |

MicroPython fits comfortably. Memory is not a constraint for this role.

### PURR-lite responsibilities

1. **USB HID keyboard** — scan 84-key matrix, send keycodes to CM5
2. **USB CDC control channel** — bidirectional data link to CM5 over same USB cable
3. **WiFi agent** — always-on network presence, runs independently of CM5 state
4. **Pi control** — send HID key sequences and CDC commands to control CM5
5. **WiFi handoff** — coordinate with CM5 over CDC when CM5 needs network ownership

---

## USB Composite Device — HID + CDC

The ESP32-S2 enumerates as a single USB composite device presenting two interfaces
simultaneously to the CM5:

```
USB Composite Device (ESP32-S2)
├── Interface 0: HID Keyboard
│   └── Sends keycodes from 84-key matrix scan
│   └── Can send programmatic key sequences (recovery triggers, macros)
│
└── Interface 1: CDC Serial (Virtual COM port)
    └── Bidirectional data channel to CM5
    └── Structured JSON or binary message protocol
    └── Pi control commands, status, WiFi handoff negotiation
```

CM5 sees both a keyboard and a serial port on the same USB connection. No extra
cables or hardware needed.

---

## HID Recovery Trigger

Because the CM5 sees the S2 as a physical keyboard, the S2 can programmatically
send any key sequence to trigger recovery:

```python
# purr_lite/recovery.py — MicroPython
import usb_hid
from adafruit_hid.keyboard import Keyboard
from adafruit_hid.keycode import Keycode

kbd = Keyboard(usb_hid.devices)

def trigger_recovery_mode():
    # Send whatever key combo PURR OS recovery listens for
    kbd.send(Keycode.CONTROL, Keycode.ALT, Keycode.R)

def trigger_reboot():
    kbd.send(Keycode.CONTROL, Keycode.ALT, Keycode.DELETE)

def send_macro(keycodes):
    kbd.send(*keycodes)
```

The CM5 cannot distinguish S2-sent keypresses from physical key presses.
The S2 retains this capability even if the CM5 OS is hung — HID input goes
below the OS level.

---

## CDC Control Channel Protocol

Simple line-based JSON protocol over CDC serial:

```python
# purr_lite/cdc_protocol.py — MicroPython
import json
import usb_cdc

serial = usb_cdc.data  # CDC interface

def send_message(msg_type, payload=None):
    msg = {"type": msg_type, "data": payload or {}}
    serial.write((json.dumps(msg) + "\n").encode())

def read_message():
    line = serial.readline()
    if line:
        return json.loads(line.decode().strip())
    return None

# Message types S2 → CM5
# {"type": "wifi_status", "data": {"connected": true, "ssid": "MyNet", "rssi": -65}}
# {"type": "wifi_handoff_ready", "data": {}}
# {"type": "recovery_triggered", "data": {}}
# {"type": "key_event", "data": {"key": "POWER", "action": "hold_3s"}}
# {"type": "temp", "data": {"c": 38.2, "rh": 52.1}}  # if SHTC3 ever wired to S2

# Message types CM5 → S2
# {"type": "request_wifi_handoff", "data": {}}
# {"type": "release_wifi", "data": {}}
# {"type": "set_keymap", "data": {"layout": "default"}}
# {"type": "firmware_update", "data": {"url": "http://..."}}
```

---

## WiFi Agent

The S2 stays on WiFi independently of CM5 state. Use cases:

- Keep network link alive while CM5 boots or sleeps
- Fetch NTP time and push to CM5 over CDC
- Pull OTA firmware updates for itself
- Act as a network watchdog — if CM5 goes silent for too long, trigger HID recovery

```python
# purr_lite/wifi_agent.py — MicroPython
import network
import ntptime
import time

wlan = network.WLAN(network.STA_IF)
wlan.active(True)

def connect(ssid, password):
    wlan.connect(ssid, password)
    timeout = 10
    while not wlan.isconnected() and timeout > 0:
        time.sleep(1)
        timeout -= 1
    return wlan.isconnected()

def sync_ntp():
    try:
        ntptime.settime()
        return True
    except:
        return False

def get_status():
    if wlan.isconnected():
        return {"connected": True, "ssid": wlan.config('essid'),
                "rssi": wlan.status('rssi')}
    return {"connected": False}
```

---

## WiFi Handoff Protocol

Software coordination between S2 and CM5. Neither owns the hardware exclusively
(both have independent WiFi silicon) — this is a logical handoff to avoid
duplicating network work.

```
Normal state: S2 owns WiFi (network agent active)
      │
CM5 sends over CDC: {"type": "request_wifi_handoff"}
      │
S2 disconnects from WiFi gracefully
S2 sends: {"type": "wifi_handoff_ready"}
      │
CM5 brings up its own WiFi stack
CM5 takes over network operations
      │
CM5 sends: {"type": "release_wifi"}
      │
S2 reconnects, resumes WiFi agent role
```

---

## Thermal Management

The SHTC3 temp/humidity sensor (U601, page 4) is on the CM5's I2C bus, not
the S2's. So thermal monitoring runs on the CM5 side — PURR OS reads SHTC3
over I2C and signals the fan controller (J601, PWM+TACHO) directly.

If temperatures exceed threshold, PURR OS can additionally send a CDC message
to the S2 for logging or alert purposes:
`{"type": "thermal_alert", "data": {"temp_c": 72, "action": "throttle"}}`

---

## Display Output Options

CattoBoardV1 has no native HDMI connector on the board. Options:

| Method | How | Notes |
|---|---|---|
| Built-in ILI9488 | SPI from CM5 | Primary display — always works |
| USB-to-HDMI dongle | Plug into USB701 | External display, no board mods |
| Future revision | Add HDMI connector to CM5 HDMI pins | Board redesign required |

---

## GPIO Voltage

Selectable via jumper RV2 on the board — either 3.3V or 1.8V for CM5 GPIO.
Set based on CM5 IO requirements. Default 3.3V for most peripherals.

---

## Known Limitations (V1)

| Issue | Impact | Workaround |
|---|---|---|
| No HDMI connector | No native video out | USB-to-HDMI dongle on USB701 |
| OTG_ID floating | No USB OTG on CM5 port | Host-only mode, acceptable |
| S2 has no display wiring | PURR-lite UI impossible | CM5 owns display — by design |
| S2 WiFi only (no BT) | No BT from co-processor | CM5 handles Bluetooth |
| S2 → CM5 link is USB only | No hardware reset authority | Software recovery via HID |
| No S2 → LoRa path | S2 can't talk LoRa directly | CM5 brokers LoRa via PURR OS |

---

## Summary

| Chip | OS | Role |
|---|---|---|
| CM5 | PURR OS (full stack) | Primary compute, display, LoRa, BT, heavy lifting |
| ESP32-S2-WROVER | HID keyboard firmware | Keyboard matrix scan, USB HID keycodes only |
| RAK3172 (STM32WLE5) | Stock RAK firmware | LoRa radio — UART to CM5 |

