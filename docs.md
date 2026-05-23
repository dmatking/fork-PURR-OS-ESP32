# PURR OS — Developer SDK Reference

**PURR** (Portable Unified Runtime & Radio) is a modular MicroPython OS for ESP32-S3 devices.  
**KITT** (Kernel Interface Translation Toolkit) is the kernel at its core.

This document covers everything you need to build apps for PURR OS: the module system, IPC bus, app lifecycle, radio handoff, device profiles, and the local development workflow.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│  boot/watchdog.py   — arms HW WDT, launches kernel             │
└────────────────────────────────┬────────────────────────────────┘
                                 │ exec
┌────────────────────────────────▼────────────────────────────────┐
│  system/kernel.app/main.py   — reads device.json,              │
│                                registers kernel modules,         │
│                                auto-discovers system/*.app       │
└────────────────────────────────┬────────────────────────────────┘
                                 │
┌────────────────────────────────▼────────────────────────────────┐
│  NanoCore  (core.py)  — supervisor + IPC pub/sub bus            │
│  ┌─────────┐  ┌───────┐  ┌────────┐  ┌─────────┐  ┌────────┐  │
│  │ display │  │ wifi  │  │  lora  │  │  input  │  │explorer│  │
│  └─────────┘  └───────┘  └────────┘  └─────────┘  └────────┘  │
│  ┌──────────────┐  ┌─────────────────────────────────────────┐  │
│  │  bridge.app  │  │  system.app  ←  your apps run here too  │  │
│  └──────────────┘  └─────────────────────────────────────────┘  │
│       all supervised asyncio tasks, communicate only via IPC    │
└─────────────────────────────────────────────────────────────────┘
```

Key design rules:
- **NanoCore owns nothing** except the module registry and the IPC bus. All hardware belongs to modules.
- **Modules communicate only through IPC**. No module holds a direct reference to another.
- **Each module is a supervised asyncio task**. A crash in one module never takes down the others.
- **device.json drives hardware**. Kernel modules are only loaded if the hardware is declared.
- **Apps are modules**. User apps launched from `/apps/` run under the same NanoCore supervisor as kernel modules — same crash isolation, same heartbeat monitoring, same restart logic.

---

## device.json

The hardware profile lives at `/system/kernel.app/device.json`. KITT reads this on boot to decide which modules to load.

```json
{
  "device":       "cattopad",
  "display":      "ili9488",
  "display_res":  [320, 480],
  "display_pins": {
    "mosi": 6, "clk": 7, "cs": 5,
    "dc": 4,  "rst": 48, "bl": 45
  },
  "touch":     "mxt336t",
  "psram":     true,
  "flash":     "16mb",
  "pi_slot":   true,
  "radios":    ["wifi", "bt", "lora"],
  "lora_pins":    { "sck": 9, "mosi": 10, "miso": 11, "cs": 8, "rst": 12, "busy": 13, "dio1": 14 },
  "lora_psk":     "AQ==",
  "buttons":      { "boot": 0 },
  "keymap":    { "boot": "SELECT", "user": "BACK" },
  "led":       null,
  "verbose":   false
}
```

| Field          | Type            | Description                                                        |
|----------------|-----------------|--------------------------------------------------------------------|
| `device`       | string          | Human-readable device name (informational only)                    |
| `display`      | string          | Driver: `ssd1306`, `ili9341`, `ili9342`, `ili9488`                 |
| `display_res`  | [width, height] | Display resolution in pixels                                       |
| `display_pins` | object          | SPI/I2C pin numbers for the display driver                         |
| `touch`        | string\|null    | Touch controller (`mxt336t`) or `null`                             |
| `psram`        | bool            | PSRAM present                                                      |
| `flash`        | string          | Flash size string (informational)                                  |
| `pi_slot`      | bool            | Pi compute module slot present                                     |
| `radios`       | [string]        | Active radios: any of `"wifi"`, `"bt"`, `"lora"`                  |
| `lora_pins`    | object          | SPI pin numbers for the LoRa radio (SX1262). Required: `sck`, `mosi`, `miso`, `cs`, `rst`, `busy`, `dio1` |
| `lora_psk`     | string\|`""`    | Base64 channel PSK. `"AQ=="` = Meshtastic default key. `""` = no encryption. 16- or 32-byte base64 = custom private channel. |
| `kern_node_id` | int\|omit       | Node ID for the KITT kern virtual node. Auto-derived from main node ID if omitted. |
| `buttons`      | object          | Button name → GPIO pin. Recognised names: `boot`, `user`, `prg`   |
| `keymap`       | object          | Button name → generic keycode (`UP`, `DOWN`, `SELECT`, `BACK`)     |
| `led`          | int\|null       | GPIO pin for status LED, or `null`                                 |
| `verbose`      | bool            | Enable boot diagnostics and the shell module                       |

### Kernel module load conditions

| Module         | Condition                                                            |
|----------------|----------------------------------------------------------------------|
| `display`      | `display` is a known driver name                                     |
| `explorer`     | display is known (stub — launches `explorer.app` or `smol.app`)     |
| `wifi`         | `"wifi"` in `radios`                                                 |
| `lora`         | `"lora"` in `radios`                                                 |
| `remoteshell`  | `"lora"` in `radios` **and** `modules/remoteass.py` exists          |
| `netboot`      | `"lora"` in `radios` **and** `modules/netboot.py` exists            |
| `input`        | `buttons` dict is present and non-empty                              |
| `shell`        | `verbose` is `true`                                                  |

Both `remoteshell` and `netboot` are **optional security modules** — deleting their files from the device silently skips registration with no kernel impact.

---

## Writing a Kernel Module

Kernel modules live in `system/kernel.app/modules/` and are registered in `kernel/main.py`.

```python
import uasyncio as asyncio

_BEAT_INTERVAL = 2000

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
            await asyncio.sleep_ms(_BEAT_INTERVAL)
```

Register it in `system/kernel.app/main.py`:

```python
from modules.mymodule import MyModule
core.register('mymodule', MyModule)                # non-critical (restarts on crash)
core.register('mymodule', MyModule, critical=True) # reboot after MAX_RESTARTS failures
```

### Module rules

- `run()` must be `async def`. It runs as a supervised asyncio task.
- Call `self._core.beat(self.NAME)` at least once every **5 seconds** or the heartbeat monitor cancels and restarts your module.
- Never hold a direct reference to another module. Use the IPC bus only.
- If `run()` returns cleanly, the supervisor treats it as an intentional stop — no restart.
- Raising an unhandled exception triggers a restart. After `MAX_RESTARTS = 5` restarts, critical modules trigger `machine.reset()`.

---

## Writing an App

Apps live in `/apps/{name}.app/main.py` and are launched at runtime by `system.app`. They run as **supervised NanoCore modules** — same crash isolation and heartbeat monitoring as kernel modules.

```python
# /apps/myapp.app/main.py

import uasyncio as asyncio

_BEAT_INTERVAL = 2000

class Module:                   # must be named exactly 'Module'
    NAME = 'myapp'              # unique, used for heartbeats and IPC

    def __init__(self, core):
        self._core = core

    async def run(self):
        print('[myapp] started')
        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._main_loop()
        finally:
            beat_task.cancel()

    async def _main_loop(self):
        q = self._core.subscribe('myapp.cmd')
        while True:
            msg = await q.get()
            # handle messages...

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
```

### Launching an app

From any module or the shell, publish to `system.app.launch`:

```python
core.publish('system.app.launch', {'app': 'myapp'})
```

`system.app` finds `/apps/myapp.app/main.py`, loads the `Module` class, and calls `core.launch()`. From that point the app is supervised like any other module.

### Stopping an app

```python
core.publish('system.app.stop', {'app': 'myapp'})
```

`system.app` calls `core.stop()` which cancels the task and removes it from supervision.

### App vs kernel module — when to use which

| | Kernel module | App |
|---|---|---|
| Lives in | `system/kernel.app/modules/` | `/apps/{name}.app/` |
| Registered by | `kernel/main.py` at boot | `system.app` at runtime |
| Supervision | NanoCore (same) | NanoCore (same) |
| Crash isolation | Yes | Yes |
| Use for | Hardware drivers, core UI, radios | User-facing features, tools |

---

## Writing a System App

System apps live in `system/{name}.app/` and are **auto-discovered by KITT at boot**. They follow the same supervised module pattern but register themselves.

```
system/
  myservice.app/
    myservice.py        ← module code + register() function
```

`myservice.py` must expose a `register(core, cfg)` function at the module level:

```python
class MyServiceModule:
    NAME = 'myservice'

    def __init__(self, core):
        self._core = core

    async def run(self):
        # ...

def register(core, cfg):
    # cfg is the full device.json dict — use it to check hardware before registering
    core.register('myservice', MyServiceModule)
```

KITT calls `register(core, cfg)` during boot for every `.app` directory in `/system/` (except `kernel.app`). The module name must not collide with existing module names.

**Naming rule**: the Python module filename must match the app directory name without `.app`. For example `system/bridge.app/bridge.py` → imported as `bridge`.

---

## NanoCore API

`NanoCore` is passed as `core` to every module's `__init__`.

### `core.register(name, factory, critical=False)`

Register a module before `core.run()` is called. `factory` is the class (called with `core` as the sole argument).

### `core.launch(name, factory, critical=False)`

Register **and immediately start supervising** a module, even after `core.run()` has started. Used by `system.app` to launch user apps at runtime. Safe to call from any async context.

```python
core.launch('myapp', MyAppModule)
```

### `core.restart(name)`

Force-restart a running module. Cancels its current task, resets its crash counter, and starts a fresh `_supervise` loop. Any module can restart any other:

```python
core.publish('core.restart', {'module': 'wifi'})   # via IPC
core.restart('wifi')                               # direct call
```

### `core.stop(name)`

Stop a module and remove it from supervision entirely. The module will not be restarted.

```python
core.stop('myapp')
```

### `core.reboot()`

Clean shutdown followed by `machine.reset()`. The watchdog re-launches the kernel.

```python
core.publish('core.reboot', {})   # via IPC from anywhere
core.reboot()                     # direct call
```

### `core.subscribe(channel) → Queue`

Subscribe to a named IPC channel. Returns an `aqueue.Queue`. Multiple subscribers on the same channel each receive every message independently.

```python
self._q = core.subscribe('wifi.status')
msg = await self._q.get()   # blocks until a message arrives
msg = self._q.get_nowait()  # raises IndexError if empty
```

### `core.publish(channel, msg)`

Broadcast a dict to all subscribers on `channel`. Never blocks — a full queue silently drops the message.

```python
core.publish('explorer.tray', {'wifi': True})
```

### `core.beat(name)`

Update the heartbeat timestamp for `name`. Must be called at least every 5 seconds.

---

## IPC Channel Reference

### Control channels

| Channel        | Publisher | Subscriber | Payload                          |
|----------------|-----------|------------|----------------------------------|
| `core.restart` | any       | NanoCore   | `{module: 'name'}`               |
| `core.reboot`  | any       | NanoCore   | `{}`                             |
| `core.crash`   | NanoCore  | any        | `{module, error, restarts}`      |
| `core.timeout` | NanoCore  | any        | `{module}`                       |
| `core.fatal`   | NanoCore  | any        | `{module}` — emitted before reset|

### App lifecycle

| Channel               | Publisher | Subscriber  | Payload                    |
|-----------------------|-----------|-------------|----------------------------|
| `system.app.launch`   | any       | system.app  | `{app: 'name'}`            |
| `system.app.stop`     | any       | system.app  | `{app: 'name'}`            |
| `system.app.launching`| system    | any         | `{name, module}`           |
| `system.app.stopped`  | system    | any         | `{name}`                   |
| `system.app.error`    | system    | any         | `{name, error}`            |
| `system.status`       | system    | any         | `{mem_free}` every 30s     |

### Input

| Channel     | Publisher | Subscriber | Payload                    |
|-------------|-----------|------------|----------------------------|
| `input.raw` | input     | bridge     | `{button, event}`          |
| `input.key` | bridge    | explorer   | `{key, event}`             |

Generic keycodes: `UP`, `DOWN`, `SELECT`, `BACK`.  
Button names map to keycodes via `keymap` in `device.json`.

### Radio handoff

| Channel             | Publisher | Subscriber | Payload                            |
|---------------------|-----------|------------|------------------------------------|
| `bridge.handoff`    | any       | bridge     | `{target: 'name'}`                 |
| `bridge.handoff.ack`| bridge    | any        | `{target, status}`                 |
| `bridge.suspend`    | bridge    | explorer   | `{target}` — UI should pause       |
| `bridge.resume`     | bridge    | explorer   | `{target}` — UI should restore     |

`status` values: `returned` (Python friend exited cleanly), `not_found`, `needs_reset` (compiled `.bin`/`.fw`), `error`.

### WiFi

| Channel      | Publisher | Subscriber | Payload                                          |
|--------------|-----------|------------|--------------------------------------------------|
| `wifi`       | any       | wifi       | `{type: 'connect', ssid, password}`              |
| `wifi`       | any       | wifi       | `{type: 'disconnect'}`                           |
| `wifi`       | any       | wifi       | `{type: 'status'}`                               |
| `wifi`       | any       | wifi       | `{type: 'scan'}`                                 |
| `wifi.status`| wifi      | any        | `{connected: bool, ifconfig: tuple\|None}`       |
| `wifi.scan`  | wifi      | any        | `{nets: [...]}`                                  |

### LoRa

| Channel         | Publisher | Subscriber | Payload                                                                          |
|-----------------|-----------|------------|----------------------------------------------------------------------------------|
| `lora`          | any       | lora       | `{type: 'status'}`                                                               |
| `lora`          | any       | lora       | `{type: 'send', to: int, text: str}`                                             |
| `lora`          | any       | lora       | `{type: 'meshme', count: int}`                                                   |
| `lora`          | any       | lora       | `{type: 'kern_sendpsk', to_node: int}`                                           |
| `lora`          | any       | lora       | `{type: 'kern_reply', to: int, text: str}` — transmit text from kern node       |
| `lora`          | any       | lora       | `{type: 'kern_setpsk', key: str}` — update PSK live (base64)                    |
| `lora.status`   | lora      | any        | `{ready: bool, spoof: bool, node: int, kern_node: int}`                          |
| `lora.rx`       | lora      | any        | `{from_node: int, to: int, id: int, hops: int, portnum: int, text: str\|None, rssi: int, snr: float}` |
| `lora.tx`       | lora      | any        | `{status: 'sent'\|'error', error: str}`                                          |
| `lora.kern.rx`  | lora      | any        | `{from_node: int, portnum: int, cmd: str\|None, rssi: int, snr: float}`          |
| `lora.kern.req` | lora      | any        | `{type: 'psk_request', from_node: int}`                                          |

`to` / `to_node` is a 32-bit node ID. Use `0xFFFFFFFF` for broadcast. `text` is `None` for non-text port numbers (position, telemetry, etc.). `portnum` mirrors the Meshtastic `PortNum` enum: `1` = text, `4` = NodeInfo, `67` = Admin.

On the emulator, LoRa starts in **spoof mode** — hardware init succeeds via the mock SPI (all-zero reads), and a fake packet is injected on `lora.rx` every 45 seconds for testing.

#### Meshtastic packet layer

`lora.py` speaks the Meshtastic wire protocol natively over the SX1262. No separate Meshtastic firmware is needed — KITT IS the Meshtastic node.

**Radio parameters (LongFast, US 915 MHz)** — must match exactly to interoperate with other Meshtastic nodes and the Meshtastic phone app:

| Parameter   | Value      |
|-------------|------------|
| Frequency   | 915.0 MHz  |
| Spreading   | SF 11      |
| Bandwidth   | 250 kHz    |
| Coding rate | 4/5        |
| Preamble    | 16 symbols |
| Sync word   | `0x1424` (Meshtastic private network) |
| TX power    | +22 dBm    |

**Wire format**: 14-byte fixed header (`to`, `from`, `id`, `flags`, `channel_hash` — all little-endian) followed by a protobuf-encoded `Data` payload. `lora.py` encodes and decodes text messages natively. Other port numbers pass through as `text: None`.

**Flood routing**: broadcast packets with `hops > 1` are re-transmitted with hop limit decremented. A 32-entry seen-packet ring prevents re-flooding packets already forwarded. Flood re-broadcasts always use the original encrypted bytes — decrypted payload is never re-transmitted.

**Node ID**: derived from the last 4 bytes of the WiFi MAC address (falls back to `machine.unique_id()`).

#### Channel encryption (AES-256-CTR)

Configured via `lora_psk` in `device.json`. All packets are encrypted on TX and decrypted on RX using AES-256-CTR with a Meshtastic-compatible nonce:

```
nonce[0:4]  = packet_id (LE)
nonce[4:8]  = 0x00000000
nonce[8:12] = from_node (LE)
nonce[12:16]= 0x00000000
```

PSK values:

| `lora_psk` value | Behaviour |
|---|---|
| `"AQ=="` | Meshtastic well-known default key (interoperates with default LongFast channel) |
| `""` or omitted | No encryption — plaintext on wire |
| 16-byte base64 | AES-128 custom key |
| 32-byte base64 | AES-256 custom private channel key |

Encryption uses `ucryptolib.aes` (ECB mode) to build the CTR keystream. If `ucryptolib` is not available (desktop MicroPython), encryption is a no-op and a warning is printed.

> **Interop note**: `_DEFAULT_PSK_BYTES` in `lora.py` are the well-known Meshtastic default key bytes. If decryption fails against real Meshtastic nodes on the default channel, verify these bytes against `meshtastic/firmware:src/mesh/CryptoEngine.cpp`.

#### Kern virtual node

KITT runs a second logical Meshtastic node identity on the same physical SX1262 — the **kern node**. From the mesh's perspective it appears as a separate device.

**How it works:**
- The kern node has its own 32-bit node ID (`kern_node_id` in `device.json`, or auto-derived as `main_node ^ 0xFFFF0000`)
- Broadcasts `NodeInfo` every 5 minutes so it shows up in the Meshtastic app's node list
- All packets addressed to the kern node ID are routed to KITT's kern handler, not to `lora.rx`
- Uses the same channel PSK as the main node

**Kern node packet routing by portnum:**

| Port | Behaviour |
|---|---|
| `1` (Text) | Parse as shell command, execute, reply with result |
| `4` (NodeInfo) | Respond with kern node's own NodeInfo |
| `67` (Admin) | Parse `SetChannel` — auto-apply any PSK change pushed by a Meshtastic app |

**Remote shell commands** (send as a Meshtastic text message to the kern node ID):

Handled by `modules/remoteass.py` — an optional kernel module. Delete the file to disable remote shell entirely with no kernel impact.

| Command | Response |
|---|---|
| `help` | Lists available commands |
| `mem` | `free:XKB used:YKB` |
| `status` | Space-separated list of running module names |
| `reboot` | Reboots the device |

Responses are sent as direct Meshtastic text replies (hop limit 1). Messages longer than 200 bytes are truncated to fit a single LoRa packet.

PSK management is intentionally excluded from the remote shell for security. PSK can be pushed at boot time via `netboot.py` (see [Net-boot](#net-boot) below), or via the Admin packet `SetChannel` path.

**`meshme N`** — sends N test broadcast text messages (`PURR 1/N`, `PURR 2/N`, …) from the kern node ID with 1.2 s between each. Use this to verify mesh propagation, check that nearby nodes hear you, or confirm your radio parameters match the network.

**PSK sharing between devices:**
1. Device A has the channel PSK; device B does not.
2. On device A: `lora kern share <device_B_kern_node_id_hex>`
3. Device A's kern node sends the PSK as a direct encrypted Meshtastic message to device B's kern node.
4. Device B's kern handler receives it and applies it via the `kern_setpsk` IPC path.
5. Both devices are now on the same encrypted channel.

#### Net-boot

`modules/netboot.py` — optional kernel module for pushing boot config over LoRa (like PXE over LoRa). Delete the file to disable with no kernel impact.

On startup, waits for LoRa ready, then broadcasts:
```
BOOTREQ <kern_node_hex>
```
A config node on the mesh responds within 30 s with:
```
BOOTSCRIPT
wifi <ssid> [password]
psk <base64-key>
notify <message>
reboot
```
Each line is executed in order. Commands:

| Command | Effect |
|---|---|
| `wifi <ssid> [pass]` | Publishes `wifi.connect {ssid, password}` |
| `psk <b64>` | Publishes `lora kern_setpsk` — updates channel PSK live |
| `notify <text>` | Publishes `explorer.notify {text, ms: 5000}` |
| `reboot` | Calls `core.reboot()` |

If no response arrives within the timeout window, netboot exits silently and the device continues normal operation. Transport security relies on the existing LoRa PSK — only use netboot on trusted mesh segments if no PSK is configured.

### Explorer / shell

| Channel            | Publisher         | Subscriber   | Payload                                |
|--------------------|-------------------|--------------|----------------------------------------|
| `explorer.tray`    | wifi, lora        | explorer.app | `{wifi: bool}` or `{lora: bool}`       |
| `explorer.notify`  | any               | explorer.app | `{text: str, ms: int}`                 |
| `display`          | explorer.app      | display      | raw draw commands (see below)          |

#### Three-tier shell system

KITT uses a layered shell architecture so the same kernel boots correctly on any display:

| Layer | File | Role |
|---|---|---|
| **explorer kernel stub** | `modules/explorer.py` | Registered by KITT; launches the right UI app |
| **explorer.app** | `apps/explorer.app/main.py` | Mac System 4/5 shell for large displays (≥ 100 px height) |
| **smol.app** | `apps/smol.app/main.py` | Text-mode shell for small displays (< 100 px, e.g. Heltec SSD1306) |
| **finder.app** | `apps/finder.app/main.py` | Standalone file browser launched from explorer.app |

The kernel stub reads `device.json` on init to check `display_res[1]`. If height < 100 it launches `smol.app` directly; otherwise it launches `explorer.app`. If `explorer.app` fails to load, it falls back to `smol.app` permanently. The stub goes quiet once a UI is confirmed running and re-launches if it stops.

**`explorer.app`** is the Mac System 4/5 inspired desktop:
- White menu bar, `PURR` and `Apps` menus, active app name, WiFi/LoRa indicators
- Light gray (`0xC618`) desktop
- Mac-style windows with candy-stripe title bars (horizontal hlines), close box, zoom box, and inverted-black selection
- Variable refresh rate: 150 ms while receiving input, 3 s while idle
- Yields display and key events entirely to any child app while it runs

**`smol.app`** is the Heltec/OLED text shell with the same variable refresh rate.

To swap in a custom UI: put it in `apps/explorer.app/main.py`. The kernel stub picks it up automatically on next boot.

---

## Display API

Draw ops are sent to the `display` channel as a `raw` message:

```python
core.publish('display', {
    'type': 'raw',
    'ops': [
        {'cmd': 'fill_rect', 'x': 0, 'y': 0, 'w': 320, 'h': 40, 'color': 0x2104},
        {'cmd': 'text',      's': 'Hello', 'x': 8, 'y': 12, 'color': 0xFFFF},
        {'cmd': 'show'},
    ]
})
```

| `cmd`       | Required fields         | Description                              |
|-------------|-------------------------|------------------------------------------|
| `fill`      | `color`                 | Fill the entire screen                   |
| `fill_rect` | `x, y, w, h, color`     | Fill a rectangle                         |
| `hline`     | `x, y, w, color`        | Horizontal line                          |
| `vline`     | `x, y, h, color`        | Vertical line                            |
| `text`      | `s, x, y, color`        | Draw a text string at pixel coordinates  |
| `show`      | —                       | Flush to display (required to see anything) |

Colors are RGB565 integers. See [Colors](#colors) below.

Higher-level message types:

| `type`    | Fields          | Description                                                  |
|-----------|-----------------|--------------------------------------------------------------|
| `text`    | `lines: [str]`  | Replace content area with a list of text lines               |
| `notify`  | `text: str`     | Show a single line at the bottom of the content area         |
| `clear`   | —               | Blank the content area                                       |

> **Content area**: `text`, `notify`, and `clear` write only to the area below the menu bar / above the taskbar for the active shell. `raw` ops are positioned absolutely — be aware that `explorer.app` reserves the top 20 px for its menu bar.

---

## Radio Handoff — Friends

Radio firmware lives in `/friends/`. Bridge looks for a Python entry point in this order:

```
/friends/{name}/main.py    ← preferred (bundle directory)
/friends/{name}.py         ← single-file script
/friends/{name}.bin        ← compiled firmware (needs machine.reset() — see below)
/friends/{name}.fw         ← compiled firmware
```

**Python friends** (`main.py` or `.py`) are `exec()`'d directly by bridge. The event loop is blocked for the duration — this is intentional, the friend firmware owns the device. `_core` is available in the execution context.

**Compiled firmware** (`.bin`, `.fw`) cannot be `exec()`'d. Bridge sends `bridge.handoff.ack` with `status: 'needs_reset'`. This path is reserved for future dual-OTA-partition support — flashing the binary to the inactive partition and rebooting into it.

**Meshtastic note**: running Meshtastic as a compiled `.bin` on the same ESP32 as KITT is not supported — it would replace MicroPython entirely. Instead, KITT implements the Meshtastic protocol natively in `lora.py` (see [LoRa / Meshtastic](#lora) above). No separate firmware or co-processor is required when LoRa is declared in `device.json`.

To trigger a handoff from your app:

```python
core.publish('bridge.handoff', {'target': 'meshtastic'})
```

Explorer subscribes to `bridge.suspend` / `bridge.resume` and pauses its draw loop automatically.

---

## Colors

`lib/colors.py` — named RGB565 constants.

```python
from colors import WIN_TEAL, WHITE, GRAY
```

| Constant     | RGB565   | Hex       |
|--------------|----------|-----------|
| `BLACK`      | `0x0000` | `#000000` |
| `WHITE`      | `0xFFFF` | `#FFFFFF` |
| `RED`        | `0xF800` | `#FF0000` |
| `GREEN`      | `0x07E0` | `#00FF00` |
| `BLUE`       | `0x001F` | `#0000FF` |
| `YELLOW`     | `0xFFE0` | `#FFFF00` |
| `CYAN`       | `0x07FF` | `#00FFFF` |
| `MAGENTA`    | `0xF81F` | `#FF00FF` |
| `ORANGE`     | `0xFC00` | `#FF8000` |
| `LIGHT_GRAY` | `0xC618` | `#C0C0C0` |
| `GRAY`       | `0x8410` | `#808080` |
| `DARK_GRAY`  | `0x4208` | `#404040` |
| `VERY_DARK`  | `0x2104` | `#202020` |
| `WIN_TEAL`   | `0x0410` | `#008080` |
| `WIN_BLUE`   | `0x000F` | `#000078` |

`0` = black, `1` = white (SSD1306 mode), any value `> 1` = raw RGB565 for color displays.

```python
def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
```

---

## SDK Tool

`sdk.py` is a Python 3 TUI that runs on your development machine (not on the device).

```bash
python3 sdk.py               # auto-detects ./device/
python3 sdk.py path/to/root  # explicit device root
```

### What it does

| Option | Action |
|--------|--------|
| `[a]`  | List installed apps in `/apps/` |
| `[n]`  | Scaffold a new app — creates `device/apps/{name}.app/main.py` with the Module template |
| `[i]`  | Install an app from a local path (copies the `.app` dir into `device/apps/`) |
| `[r]`  | Remove an installed app |
| `[k]`  | List kernel modules with active/inactive state derived from `device.json` |
| `[s]`  | List system apps in `device/system/` |
| `[c]`  | Edit any `device.json` field inline |
| `[?]`  | Full IPC channel reference |

The scaffold template includes the correct `Module` class structure, heartbeat loop, and usage comments.

---

## Local Development

### Requirements

- [MicroPython Unix port](https://github.com/micropython/micropython) — build: `make -C ports/unix`
- Python 3 with tkinter (for the GUI emulator)

### Quickstart

```bash
./emulate                        # starts GUI + kernel (Ingenico 320×480, 1× scale)
./emulate --device box3          # ESP32-S3-Box-3 320×240
./emulate --scale 2              # 2× pixel scaling for readability
./emulate stop                   # stops both
./emulate restart                # restart kernel only (display stays open)
./emulate log                    # tail the kernel log
```

Device presets: `ingenico` (320×480, default), `box3` (320×240), `cattopad` (320×480).

### Manual control

```bash
micropython run.py device         # kernel only, terminal display fallback
python3 emulator.py --scale 2 &   # GUI window first, then kernel
micropython run.py device
```

### What `run.py` patches

`run.py` makes the kernel run on your Mac without any hardware:

- Absolute `/paths` are rewritten to `device/...`
- `machine.Pin`, `machine.SPI`, `machine.WDT` are no-op stubs
- `machine.SPI.write_readinto` returns zeros — LoRa status check passes as spoof mode
- `network.WLAN` is stubbed (mock state, no real WiFi)
- `utime.sleep_ms` / `utime.sleep` are no-ops
- `display_factory.make_display()` tries `_SocketDisplay` (GUI emulator on port 8765) first, falls back to `_TerminalDisplay` (ASCII grid in terminal)

### Shell

When `"verbose": true` in `device.json`, the `shell` module starts alongside the kernel:

```
kitt> help
```

**System commands:**

| Command | Description |
|---|---|
| `mem` | Free / used / total RAM with % |
| `uptime` | `HH:MM:SS` since kernel boot |
| `status` | Table of modules: state + restart count |
| `reboot` | Clean reboot via `core.reboot()` |
| `pub <topic> <json>` | Publish a raw IPC message — useful for triggering any module |

**Display commands:**

| Command | Description |
|---|---|
| `echo <text>` | Show text on display (`\n` for newlines) |
| `notify <text>` | Bottom notification bar |
| `clear` | Clear content area |

**Radio commands:**

| Command | Description |
|---|---|
| `lora send <text>` | Broadcast from main user node |
| `lora status` | Trigger `lora.status` publish |
| `lora kern` | Show kern + main node IDs, encryption state |
| `lora kern share <hex>` | Send PSK to kern node at `<hex>` node ID |
| `meshme <N>` | Send N test broadcasts from kern node (1.2 s apart) |
| `wifi status` | Current connection state + IP |
| `wifi connect <ssid> [pass]` | Connect to network |
| `wifi scan` | List nearby APs with channel + RSSI |
| `wifi off` | Disconnect |

Example PSK share flow:
```
# On device A (has PSK), after learning device B's kern node ID:
kitt> lora kern
kern node:  2152BEEF   ← tell this to device B's operator
main node:  DEADBEEF
encryption: ON

# On device A, sending PSK to device B:
kitt> lora kern share 6543abcd

# On device B's shell after receiving:
kitt> lora kern
encryption: ON
```

### Testing radio handoff

```bash
mkdir -p device/friends/meshtastic
echo 'print("hello from meshtastic")' > device/friends/meshtastic/main.py
```

Then from the shell:
```
kitt> publish bridge.handoff {"target": "meshtastic"}
```

---

## aqueue.Queue

`lib/aqueue.py` — async FIFO queue (drop-in for `asyncio.Queue`, removed in MicroPython 1.20+).

```python
from aqueue import Queue

q = Queue()
q.put_nowait({'event': 'ready'})   # non-blocking enqueue
msg = await q.get()                # blocks until an item is available
msg = q.get_nowait()               # raises IndexError if empty
```

`NanoCore.subscribe()` returns one of these. You rarely need to instantiate it directly.

---

## File Layout

```
device/                         ← device filesystem root (maps to / on hardware)
├── boot/
│   └── watchdog.py             # HW WDT + kernel launcher
├── main.py                     # Entry point
│
├── system/
│   ├── kernel.app/
│   │   ├── main.py             # Device profile loader + module registration + auto-discovery
│   │   ├── core.py             # NanoCore: supervisor, IPC bus, heartbeat monitor
│   │   ├── device.json         # Hardware profile (edit for your device)
│   │   └── modules/
│   │       ├── display.py          # Display driver wrapper + IPC
│   │       ├── explorer.py         # Shell stub — routes to explorer.app or smol.app based on screen size
│   │       ├── remoteass.py        # Remote shell over LoRa kern node (optional — delete to strip)
│   │       ├── netboot.py          # LoRa net-boot config push (optional — delete to strip)
│   │       ├── explorer_lvgl.py    # LVGL shell (not registered — swap in via main.py if desired)
│   │       ├── lvgl_layout.py      # LVGL layout definitions (320×480)
│   │       ├── purr_ui.py          # Grid launcher shell (not yet registered)
│   │       ├── purr_ui_layout.py   # PURR UI layout: 2×2 tile grid, status bar, F-key bar
│   │       ├── wifi.py             # WiFi management
│   │       ├── lora.py             # SX1262 driver + Meshtastic packet layer + AES-256-CTR encryption + kern virtual node
│   │       ├── input.py            # Button polling + raw event publish
│   │       └── shell.py            # Interactive REPL: system, display, lora, meshme, wifi commands
│   │
│   ├── bridge.app/
│   │   └── bridge.py           # Button→keycode translation + radio handoff
│   │
│   └── system.app/
│       └── system.py           # App lifecycle manager + GC monitor
│
├── lib/
│   ├── aqueue.py               # Async queue
│   ├── colors.py               # Named RGB565 constants
│   ├── display_factory.py      # Selects and constructs the right display driver
│   ├── ili9341.py              # ILI9341/9342/9488 SPI display driver
│   └── ssd1306.py              # SSD1306 I2C OLED driver
│
├── apps/                       # User app bundles go here
│   ├── explorer.app/
│   │   └── main.py             # Mac System 4/5 shell (large displays, ≥ 100 px height)
│   ├── smol.app/
│   │   └── main.py             # Text-mode shell (small displays, e.g. Heltec SSD1306)
│   ├── finder.app/
│   │   └── main.py             # File browser (launched from explorer.app)
│   └── myapp.app/
│       └── main.py             # Must expose a Module class
│
└── friends/                    # Radio firmware goes here
    └── meshtastic/
        └── main.py             # Python friends: exec'd by bridge

run.py                          # Dev runner (MicroPython Unix port)
emulator.py                     # tkinter GUI display emulator
emulate                         # Control script: start/stop/restart/display/kernel/log
sdk.py                          # Developer SDK TUI
```

---

## Supported Hardware

| Device                    | MCU             | Flash | Display           | Tested via            |
|---------------------------|-----------------|-------|-------------------|-----------------------|
| CattoPad (Ingenico M5000) | ESP32-S3-N16R8  | 16 MB | 320×480 ILI9488   | Hardware              |
| ESP32-S3-Box-3            | ESP32-S3        | 16 MB | 320×240 ILI9342   | run.py + emulator     |
| Heltec WiFi LoRa 32 V3    | ESP32-S3FN8     | 8 MB  | 128×64 SSD1306    | Wokwi                 |

Add a new device by creating a `device.json` with the correct `display`, `display_res`, `display_pins`, `radios`, and `buttons` entries — no code changes required.
