# Integrating LVGL Explorer with PURR OS

This guide shows how to use the new Windows CE LVGL explorer module with the PURR OS kernel.

## Quick Start

### Step 1: Choose Your Explorer

Two versions are available:

- **`explorer.py`** (current) — Minimal mode. Shows last key pressed. Pure primitives-based.
- **`explorer_lvgl.py`** (new) — Full LVGL mode with desktop, taskbar, file manager. Falls back to minimal if LVGL unavailable.

### Step 2: Switch to LVGL Explorer

Option A: Simple file swap (recommended)
```bash
cd /Users/pastorcatto/Documents/Projects/PURR-OS/PURR-OS-ESP32/system/kernel.app/modules/
cp explorer.py explorer_minimal.py   # Keep the old version as backup
cp explorer_lvgl.py explorer.py      # Use LVGL version
```

Option B: Edit main.py to select module
```python
# In system/kernel.app/main.py
from modules.explorer_lvgl import ExplorerModule  # Use LVGL version
# OR
from modules.explorer import ExplorerModule  # Use minimal version
```

### Step 3: Verify Device Configuration

Ensure your `device.json` has:
```json
{
  "display": "ili9342",
  "display_res": [320, 240],
  "buttons": {"boot": 0}
}
```

## Module Features

### LVGL Mode (automatic if LVGL available)

When LVGL is available:
- Windows CE desktop background (teal)
- Bottom taskbar with Start button
- System tray (WiFi, Bluetooth, clock)
- Floating explorer window with file list
- Desktop icons
- Keyboard navigation (UP, DOWN, SELECT, BACK)

### Minimal Mode (fallback if LVGL unavailable)

Automatic fallback when LVGL is not found:
- Simple black background
- Shows last key pressed for 1 second
- Full keyboard event logging
- Zero dependencies

## Keyboard Mapping

The module subscribes to `input.key` from the input module.

Mapped keycodes (set in emulator.py):
```python
'Return'  → SELECT   (open file)
'Escape'  → BACK     (close/back)
'Up'      → UP       (navigate)
'Down'    → DOWN     (navigate)
'Left'    → LEFT     (previous pane)
'Right'   → RIGHT    (next pane)
```

## IPC Channels

### Subscribed Channels

- **`input.key`** — Keyboard events (press/release)
  ```python
  {'key': 'SELECT', 'event': 'press'}
  ```

- **`explorer.tray`** — System tray updates (future)
  ```python
  {'wifi': True, 'battery': 75, 'time': '14:32'}
  ```

### Published Channels

- **`display`** — In minimal mode, sends raw draw ops
  ```python
  {'type': 'raw', 'ops': [{'cmd': 'fill', 'color': 0}, ...]}
  ```

## Testing Locally

### Terminal Test (no GUI)
```bash
cd /Users/pastorcatto/Documents/Projects/PURR-OS/PURR-OS-ESP32/
micropython run.py
```

Output (minimal mode on Mac):
```
[explorer] LVGL unavailable, using minimal mode
[explorer] key: UP
[explorer] key: SELECT
```

### GUI Test with Emulator
```bash
# Terminal 1: GUI emulator
python3 emulator.py

# Terminal 2: Kernel (in same directory)
micropython run.py
```

Click in the emulator window and press arrow keys + Enter to send input.

### Standalone LVGL Layout Test (requires lv_bindings)
```bash
python3 lvgl_test.py
```

This tests the Windows CE UI layout without the kernel running.

## Architecture

```
kernel.app/
├── main.py              ← registers ExplorerModule from explorer.py
├── core.py              ← NanoCore IPC bus
├── device.json          ← hardware profile
└── modules/
    ├── explorer.py      ← active explorer (you choose which version)
    ├── explorer_lvgl.py ← new LVGL version (auto-fallback)
    ├── lvgl_layout.py   ← Windows CE UI definitions
    ├── display.py       ← display driver wrapper
    └── input.py         ← keyboard input
```

## Troubleshooting

### "LVGL unavailable, using minimal mode"
- LVGL is not available in this MicroPython build
- Minimal mode still works for keyboard input testing
- To use LVGL, you need a build with lv_bindings

### "failed to import lvgl modules, falling back"
- LVGL was found but `lvgl_layout.py` could not be imported
- Check `/system/kernel.app/modules/lvgl_layout.py` exists
- Verify no syntax errors: `python3 -m py_compile lvgl_layout.py`

### Module crashes on startup
- Check `device.json` for correct display configuration
- Verify display driver is working (try `explorer.py` minimal mode first)
- Check heap size if LVGL is running out of memory

### Keys not responding
- Verify input module is running: check device.json has `"buttons"`
- Check emulator keyboard bindings (see emulator.py `_KEY_MAP`)
- Monitor logs: look for `[explorer] key: ...` messages

## Next Steps

### Enhance the UI
- Edit `lvgl_layout.py` to customize colors, layout, or components
- Add more desktop icons or tray indicators
- Implement file browsing (currently mock files)

### Add Features
- Implement Start menu (currently placeholder)
- Add touch input support
- Add window management (drag, resize, minimize)
- Integrate with WiFi module for live status

### Optimize
- Profile memory usage (LVGL can be memory-intensive)
- Reduce refresh rate if needed
- Optimize display driver if running slow

## References

- [LVGL_LAYOUT.md](LVGL_LAYOUT.md) — Layout details and customization
- [docs.md](docs.md) — Full PURR OS architecture
- [EMULATOR_KEYBOARD.md](EMULATOR_KEYBOARD.md) — Emulator keyboard setup
