# PURR OS Build Log

## Project Summary

Modular MicroPython OS for ESP32-S3. Kernel: KITT (NanoCore supervisor + IPC pub/sub bus).
Primary target: CattoPad (Ingenico Move 5000 cyberdeck, ILI9488 320×480).
Dev target: ESP32-S3-Box-3 (ILI9342 320×240).

Goal: SDK others can extend. Clean IPC module system, device.json-driven hardware profiles, upstream firmware (Meshtastic, Bruce) runs stock via radio handoff.

---

## Architecture

```
device/
  boot/watchdog.py      Hardware WDT + kernel launcher
  main.py               Entry point
  system/kernel.app/
    core.py             NanoCore: supervisor, IPC pub/sub (aqueue.Queue), heartbeat monitor
    main.py             Module loader + async task runner (auto-discovers system/*.app)
    modules/
      display.py        Display driver abstraction
      explorer.py       Shell/taskbar (minimal keyboard display, fallback)
      explorer_lvgl.py  Full LVGL Windows CE UI with auto-fallback to minimal
      lvgl_layout.py    Pure LVGL layout definitions (desktop, taskbar, tray)
      wifi.py           WiFi module
      input.py          Button/GPIO polling → publishes input.raw (raw button name)
      lora.py           LoRa stub
      shell.py          REPL shell
  system/bridge.app/
    bridge.py           input.raw → input.key translation; radio handoff exec broker
  system/system.app/
    system.py           App lifecycle manager; GC monitor
  lib/
    aqueue.py           Async queue (IPC backbone)
    colors.py           RGB565 constants
    display_factory.py  Device-agnostic display init
    ili9341.py          ILI9341/ILI9488 driver
    ssd1306.py          SSD1306 OLED driver
  apps/                 User app bundles go here ({name}.app/main.py)
  friends/              Radio firmware goes here ({name}/main.py or {name}.py or .bin/.fw)
run.py                  MicroPython Unix port runner (dev)
emulator.py             tkinter GUI emulator (dev)
emulate                 Control script: start/stop/restart/display/kernel/log
```

---

## Completed Work

### NanoCore / KITT
- Supervisor loop with async task management
- IPC pub/sub bus via `aqueue.Queue`
- Heartbeat monitor: 5s timeout, max 5 restarts before module is killed
- WDT fed by NanoCore every 2s; passed through exec globals from watchdog

### Boot
- Hardware WDT via `boot/watchdog.py`
- Kernel launch from `main.py`
- WDT fix: `watchdog.py` passes `_wdt` through exec globals so NanoCore feeds it

### KITT Auto-Discovery
- `register_apps()` in `kernel/main.py` auto-scans `system/*.app/`, skips `kernel.app`
- Each app exposes `register(core, cfg)` in its root module; KITT calls it automatically
- No hardcoding in kernel — all apps are self-contained

### Dev Environment
- `device/` container: OS filesystem isolated from tooling
- `run.py` path proxy: `_OsPatch` replaces `sys.modules['os']` so absolute paths redirect to `PROJECT_ROOT`; `builtins.open` patched similarly
- `./emulate` passes `$SCRIPT_DIR/device` as device root automatically

### Modules
- `display` — display abstraction layer
- `explorer_lvgl.py` — Windows CE-style shell; auto-falls back to minimal mode
  - Start menu: scans `/apps/` and `/friends/`, populates Apps + Friends sections
  - Tray: wifi icon (black=connected, gray=not)
  - `bridge.suspend` / `bridge.resume` subscriptions — pauses LVGL draw during handoff
- `wifi` — WiFi connection management; publishes `{wifi: bool}` to `explorer.tray`
- `input` — GPIO/button polling; publishes `input.raw`
- `lora` — stub (placeholder)
- `shell` — async REPL

### bridge.app
- `input.raw` → `input.key` translation via `device.json` keymap
- Radio handoff: publishes `bridge.suspend`, execs Python friend from `/friends/`, publishes `bridge.resume`
- Binary firmware (`.bin`/`.fw`): detected, sends `needs_reset` ack (requires machine-level handoff)
- Entry lookup order: `/friends/{name}/main.py` → `/friends/{name}.py` → `.bin` → `.fw`

### system.app
- App lifecycle: subscribes to `system.app.launch`, finds `/apps/{name}.app/main.py`, execs in isolated context
- Publishes `system.app.launching`, `system.app.exited`, `system.app.error`
- GC loop: `gc.collect()` every 30s, publishes `system.status` with `mem_free`

---

## Pending / Next Steps

- [ ] **Meshtastic test** — drop a Python friend into `/friends/meshtastic/main.py`, trigger handoff, verify exec + resume
- [ ] **More tray indicators** — BT and LoRa status icons (same pattern as WiFi)
- [ ] **bridge.app: machine-level handoff** — for compiled `.bin`/`.fw`: write firmware path to NVS / flag file, then `machine.reset()` to boot into it
- [ ] **system.app: OTA** — placeholder for over-the-air update flow
- [ ] **system.app: running app registry** — track what's currently running, expose via IPC

---

## IPC Channel Reference

| Channel | Publisher | Subscriber | Payload |
|---|---|---|---|
| `input.raw` | input | bridge | `{button, event}` |
| `input.key` | bridge, emulator | explorer | `{key, event}` |
| `bridge.handoff` | explorer, any | bridge | `{target}` |
| `bridge.handoff.ack` | bridge | any | `{target, status}` |
| `bridge.suspend` | bridge | explorer | `{target}` |
| `bridge.resume` | bridge | explorer | `{target}` |
| `explorer.tray` | wifi, system | explorer | status dict |
| `system.app.launch` | explorer | system | `{app: name}` |
| `system.app.launching` | system | any | `{name}` |
| `system.app.exited` | system | any | `{name}` |
| `system.app.error` | system | any | `{name, error}` |
| `system.status` | system | any | `{mem_free}` |
| `display` | explorer | display | render commands |

---

## Dev Commands

```bash
# Run with MicroPython Unix port
micropython run.py device

# Run with tkinter emulator
./emulate start         # starts display window + kernel
./emulate stop          # stops both
./emulate restart       # restarts kernel only
./emulate log           # tail kernel log

# Drop a Python friend for handoff testing
mkdir -p device/friends/meshtastic
echo 'print("hello from meshtastic")' > device/friends/meshtastic/main.py
```
