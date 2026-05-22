# PURR OS — SDK Reference

**PURR** (Portable Unified Runtime & Radio) is a modular MicroPython OS for ESP32-S3 devices.  
**KITT** (Kernel Interface Translation Toolkit) is the kernel at its core.

This document covers the public SDK surface: how to write modules, the IPC bus, the display API, device profiles, and the local development workflow.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  boot/watchdog.py   — arms HW WDT, launches kernel          │
└──────────────────────────────┬──────────────────────────────┘
                               │ exec
┌──────────────────────────────▼──────────────────────────────┐
│  system/kernel.app/main.py   — reads device.json,           │
│                                registers modules             │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│  NanoCore  (core.py)                                        │
│  ┌─────────┐  ┌─────────┐  ┌──────────┐  ┌─────────────┐  │
│  │ display │  │  wifi   │  │  input   │  │  explorer   │  │
│  └─────────┘  └─────────┘  └──────────┘  └─────────────┘  │
│         asyncio tasks, supervised, IPC via pub/sub bus      │
└─────────────────────────────────────────────────────────────┘
```

Key design rules:
- **NanoCore owns nothing** except the module registry and the IPC bus. All hardware belongs to modules.
- **Modules communicate only through IPC**. No module holds a direct reference to another.
- **Each module is an independent asyncio task**. A crash in one module never takes down the others.
- **device.json drives everything**. Modules are only imported and started if the hardware is declared.

---

## device.json

The hardware profile at `/system/kernel.app/device.json`. KITT reads this on boot to decide which modules to load.

```json
{
  "device":       "cattopad",
  "display":      "ili9488",
  "display_res":  [320, 480],
  "display_pins": {
    "mosi": 6, "clk": 7, "cs": 5,
    "dc": 4,  "rst": 48, "bl": 45
  },
  "touch":   "mxt336t",
  "psram":   true,
  "flash":   "16mb",
  "pi_slot": true,
  "radios":  ["wifi", "bt", "lora"],
  "buttons": { "boot": 0, "user": 14 },
  "led":     null,
  "verbose": false
}
```

| Field          | Type            | Description                                                  |
|----------------|-----------------|--------------------------------------------------------------|
| `device`       | string          | Human-readable device name (informational only)              |
| `display`      | string          | Driver: `ssd1306`, `ili9341`, `ili9342`, `ili9488`           |
| `display_res`  | [width, height] | Display resolution in pixels                                 |
| `display_pins` | object          | SPI/I2C pin numbers for the display driver                   |
| `touch`        | string\|null    | Touch controller (`mxt336t`) or `null`                       |
| `psram`        | bool            | PSRAM present                                                |
| `flash`        | string          | Flash size string (informational)                            |
| `pi_slot`      | bool            | Pi compute module slot present                               |
| `radios`       | [string]        | Active radios: any of `"wifi"`, `"bt"`, `"lora"`            |
| `buttons`      | object          | Button name → GPIO pin number. Recognised names: `boot`, `user`, `prg` |
| `led`          | int\|null       | GPIO pin for status LED, or `null`                           |
| `verbose`      | bool            | Enable boot diagnostics and the shell module                 |

### Module load conditions (main.py)

| Module     | Condition                                          |
|------------|----------------------------------------------------|
| `display`  | `display` is a known driver name                  |
| `explorer` | display is known **and** display height ≥ 200 px  |
| `wifi`     | `"wifi"` in `radios`                              |
| `lora`     | `"lora"` in `radios`                              |
| `input`    | `buttons` dict is present and non-empty           |
| `shell`    | `verbose` is `true`                               |

---

## Writing a Module

Every module is a class with three required items:

```python
class MyModule:
    NAME = 'mymodule'           # unique string — used for heartbeats and crash reports

    def __init__(self, core):   # receives the NanoCore instance
        self._core = core
        self._q    = core.subscribe('mymodule')   # subscribe to an IPC channel

    async def run(self):        # main coroutine — runs for the lifetime of the module
        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._loop()
        finally:
            beat_task.cancel()

    async def _loop(self):
        while True:
            msg = await self._q.get()
            # handle messages...

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(2000)
```

### Rules

- `run()` must be an `async def`. It runs as a supervised asyncio task.
- Call `self._core.beat(self.NAME)` at least once every **5 seconds** or the heartbeat monitor will cancel and restart your module.
- Never hold a direct reference to another module. Use the IPC bus to communicate.
- If `run()` returns cleanly, the supervisor treats it as an intentional stop — no restart.
- Raising an unhandled exception triggers a restart. After `MAX_RESTARTS = 5` restarts, critical modules trigger `machine.reset()`.

### Registering a module

In `system/kernel.app/main.py`:

```python
from modules.mymodule import MyModule
core.register('mymodule', MyModule)                    # non-critical
core.register('mymodule', MyModule, critical=True)     # reboot if it fails repeatedly
```

---

## NanoCore API

`NanoCore` is passed as `core` to every module's `__init__`.

### `core.register(name, factory, critical=False)`

Register a module class before `core.run()` is called. `factory` is the class itself (called with `core` as the sole argument). `critical=True` triggers a reboot if the module exceeds the restart limit.

### `core.subscribe(channel) → Queue`

Subscribe to a named IPC channel. Returns an `aqueue.Queue`. Multiple subscribers on the same channel each receive every message independently.

```python
self._q = core.subscribe('wifi.status')
msg = await self._q.get()   # blocks until a message arrives
```

### `core.publish(channel, msg)`

Broadcast a dict to all subscribers on `channel`. Never blocks — a full queue drops the message silently.

```python
core.publish('display', {'type': 'notify', 'text': 'WiFi connected'})
```

### `core.beat(name)`

Update the heartbeat timestamp for `name`. Must be called at least every 5 seconds.

---

## IPC Channel Reference

### `display` — draw commands to the display module

| Message type | Fields                          | Description                                           |
|--------------|---------------------------------|-------------------------------------------------------|
| `text`       | `lines: [str]`                  | Replace content area with a list of text lines        |
| `notify`     | `text: str`                     | Show a single line at the bottom of the content area  |
| `clear`      | —                               | Blank the content area                                |
| `raw`        | `ops: [op_dict]`                | Execute an array of draw ops directly (see below)     |

#### `raw` draw ops

Each op is a dict with a `cmd` key:

| `cmd`        | Required fields             | Description                            |
|--------------|-----------------------------|----------------------------------------|
| `fill`       | `color`                     | Fill the entire screen                 |
| `fill_rect`  | `x, y, w, h, color`         | Fill a rectangle                       |
| `hline`      | `x, y, w, color`            | Horizontal line                        |
| `vline`      | `x, y, h, color`            | Vertical line                          |
| `text`       | `s, x, y` + optional `color`| Draw a text string at pixel coords     |
| `show`       | —                            | Flush to screen (required to see changes) |

Colors are RGB565 integers. See [colors.py](#colors).

**Example** — draw a filled bar with a label:
```python
self._core.publish('display', {
    'type': 'raw',
    'ops': [
        {'cmd': 'fill_rect', 'x': 0, 'y': 200, 'w': 320, 'h': 40, 'color': 0x2104},
        {'cmd': 'text',      's': 'Hello', 'x': 8, 'y': 212, 'color': 0xFFFF},
        {'cmd': 'show'},
    ]
})
```

> **Content area rule**: `text`, `notify`, and `clear` messages write only to the content area (full height minus the 32 px taskbar on displays ≥ 200 px tall). `raw` ops are positioned absolutely — it is the sender's responsibility not to stomp the taskbar.

---

### `explorer.tray` — tray state updates

Messages to this channel update the explorer taskbar indicators.

```python
core.publish('explorer.tray', {'wifi': True})    # show W+
core.publish('explorer.tray', {'wifi': False})   # show W-
```

| Field   | Type | Description             |
|---------|------|-------------------------|
| `wifi`  | bool | WiFi connected state    |

---

### `wifi` — commands to the wifi module

```python
core.publish('wifi', {'type': 'connect',    'ssid': 'MyNet', 'password': 'pass'})
core.publish('wifi', {'type': 'disconnect'})
core.publish('wifi', {'type': 'status'})
core.publish('wifi', {'type': 'scan'})
```

---

### `wifi.status` — wifi connection events

Published by the wifi module when connection state changes or a `status` command is sent.

```python
{'connected': True,  'ifconfig': ('192.168.1.10', '255.255.255.0', '192.168.1.1', '8.8.8.8')}
{'connected': False, 'ifconfig': None}
```

---

### `wifi.scan` — scan results

Published in response to a `{'type': 'scan'}` message on the `wifi` channel.

```python
{'nets': [(ssid, bssid, channel, rssi, authmode, hidden), ...]}
```

---

### `input.key` — button events

Published by the input module on every debounced press/release.

```python
{'key': 'SELECT', 'event': 'press'}
{'key': 'SELECT', 'event': 'release'}
```

Generic keycodes: `UP`, `DOWN`, `SELECT`, `BACK`  
Physical button names map to keycodes via the keymap in `input.py`.

---

### `core.crash` — module crash notification

Published by NanoCore when a supervised module raises an exception.

```python
{'module': 'wifi', 'error': 'timeout', 'restarts': 2}
```

---

### `core.timeout` — heartbeat timeout

Published by NanoCore when a module misses its heartbeat window.

```python
{'module': 'explorer'}
```

---

### `core.fatal` — critical module exceeded restart limit

Published just before NanoCore calls `machine.reset()`.

```python
{'module': 'display'}
```

---

## Colors

`lib/colors.py` — named RGB565 constants usable in any draw op.

```python
from colors import WIN_TEAL, WHITE, GRAY

ops = [
    {'cmd': 'fill_rect', 'x': 0, 'y': 200, 'w': 320, 'h': 40, 'color': WIN_TEAL},
    {'cmd': 'text', 's': 'PURR', 'x': 5, 'y': 212, 'color': WHITE},
]
```

| Constant     | RGB565   | Approximate hex |
|--------------|----------|-----------------|
| `BLACK`      | `0x0000` | `#000000`       |
| `WHITE`      | `0xFFFF` | `#FFFFFF`       |
| `RED`        | `0xF800` | `#FF0000`       |
| `GREEN`      | `0x07E0` | `#00FF00`       |
| `BLUE`       | `0x001F` | `#0000FF`       |
| `YELLOW`     | `0xFFE0` | `#FFFF00`       |
| `CYAN`       | `0x07FF` | `#00FFFF`       |
| `MAGENTA`    | `0xF81F` | `#FF00FF`       |
| `ORANGE`     | `0xFC00` | `#FF8000`       |
| `LIGHT_GRAY` | `0xC618` | `#C0C0C0`       |
| `GRAY`       | `0x8410` | `#808080`       |
| `DARK_GRAY`  | `0x4208` | `#404040`       |
| `VERY_DARK`  | `0x2104` | `#202020`       |
| `WIN_TEAL`   | `0x0410` | `#008080`       |
| `WIN_BLUE`   | `0x000F` | `#000078`       |

**Encoding rule**: `0` = black, `1` = white (SSD1306 compatible), any value `> 1` = raw RGB565 for color displays.

To convert an RGB tuple to RGB565:
```python
def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
```

---

## Display Layout

```
┌─────────────────────────────────┐  ▲
│                                 │  │
│         content area            │  h - 32px
│   (text, notify, clear, raw)    │  │
│                                 │  ▼
├─────────────────────────────────┤  ← y = h - 32
│  [PURR]  W+  LoRa   00:42      │  32px taskbar (explorer)
└─────────────────────────────────┘
         w = 320 (CattoPad)
```

- The taskbar is drawn by `explorer.py` via `raw` IPC ops every 1 second.
- The content area is `h - 32` px for displays ≥ 200 px tall. On smaller displays (e.g. 128×64 SSD1306) the full height is the content area and no taskbar is drawn.
- `display.py` methods (`draw_text`, `notify`, `clear`) automatically respect this boundary.

---

## Local Development

### Requirements

- [MicroPython Unix port](https://github.com/micropython/micropython) — build from source: `make -C ports/unix`
- Python 3 with tkinter (for the GUI emulator)

### Running the kernel

```bash
# Terminal only (no GUI needed)
micropython run.py

# With GUI emulator — start emulator first, then kernel
python3 emulator.py &
micropython run.py
```

`run.py` patches the MicroPython environment so the kernel runs on your Mac:
- Absolute `/paths` are rewritten to `PROJECT_ROOT/paths`
- `machine.Pin`, `machine.SPI`, `machine.WDT` are stubbed (no-op)
- `network.WLAN` is stubbed (mock state, no real WiFi)
- `utime.sleep_ms` / `utime.sleep` are no-ops (avoids hardware init delays)
- `display_factory.make_display()` is replaced: tries `_SocketDisplay` (GUI) first, falls back to `_TerminalDisplay` (ASCII grid in terminal)

### Emulator options

```bash
python3 emulator.py --width 320 --height 240 --scale 2   # default
python3 emulator.py --1to1                               # 1 display-pixel = 1 screen pixel
python3 emulator.py --width 128 --height 64 --scale 4   # SSD1306 enlarged
```

The emulator listens on `127.0.0.1:8765` for newline-delimited JSON draw commands.

### Shell module

When `"verbose": true` in `device.json`, the `shell` module starts an interactive REPL alongside the kernel:

```
kitt> echo Hello World
kitt> notify WiFi connected
kitt> clear
kitt> status
kitt> help
```

---

## aqueue.Queue

`lib/aqueue.py` — async FIFO queue. Drop-in replacement for `asyncio.Queue` (removed in MicroPython 1.20+).

```python
from aqueue import Queue

q = Queue()
q.put_nowait({'event': 'ready'})   # non-blocking enqueue
msg = await q.get()                # blocks until an item is available
```

`NanoCore.subscribe()` returns one of these. You rarely need to instantiate it directly.

---

## File Layout

```
/
├── boot/
│   └── watchdog.py          # HW WDT + kernel launcher
│
├── system/
│   └── kernel.app/
│       ├── main.py          # Device profile loader + module registration
│       ├── core.py          # NanoCore: supervisor, IPC bus, heartbeat monitor
│       ├── device.json      # Hardware profile (edit this for your device)
│       └── modules/
│           ├── display.py   # Display driver wrapper + IPC
│           ├── explorer.py  # Taskbar / tray UI shell
│           ├── wifi.py      # WiFi management
│           ├── lora.py      # LoRa (placeholder)
│           ├── input.py     # Button polling + generic keycodes
│           └── shell.py     # Interactive REPL (verbose mode)
│
├── lib/
│   ├── aqueue.py            # Async queue (asyncio.Queue replacement)
│   ├── colors.py            # Named RGB565 constants
│   ├── display_factory.py   # Selects and constructs the right display driver
│   ├── ili9341.py           # ILI9341/9342/9488 SPI display driver
│   └── ssd1306.py           # SSD1306 I2C OLED driver
│
├── run.py                   # Local dev runner (MicroPython Unix port)
├── emulator.py              # tkinter GUI display emulator
└── wokwi.toml               # Wokwi simulator config
```

---

## Supported Hardware

| Device                   | MCU               | Flash | Display                   | Tested via  |
|--------------------------|-------------------|-------|---------------------------|-------------|
| CattoPad (Ingenico M5000)| ESP32-S3-N16R8    | 16 MB | 320×480 ILI9488           | Hardware    |
| ESP32-S3-Box-3           | ESP32-S3          | 16 MB | 320×240 ILI9342           | run.py + emulator |
| Heltec WiFi LoRa 32 V3   | ESP32-S3FN8       | 8 MB  | 128×64 SSD1306            | Wokwi       |

Add a new device by creating a `device.json` with the correct `display`, `display_res`, `display_pins`, `radios`, and `buttons` entries — no code changes required.
