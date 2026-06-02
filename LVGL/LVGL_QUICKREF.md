# LVGL Explorer — Quick Reference

## Files Overview

| File | Purpose | Language |
|------|---------|----------|
| `system/kernel.app/modules/lvgl_layout.py` | UI component definitions | Pure MicroPython/LVGL |
| `system/kernel.app/modules/explorer_lvgl.py` | Kernel integration (replaces explorer.py) | MicroPython |
| `lvgl_test.py` | Standalone layout tester | Python 3 |
| `LVGL_LAYOUT.md` | Detailed layout documentation | Markdown |
| `LVGL_INTEGRATION.md` | How to use in PURR OS | Markdown |

## 60-Second Setup

```bash
# Step 1: Backup current explorer
cp system/kernel.app/modules/explorer.py system/kernel.app/modules/explorer_minimal.py

# Step 2: Install LVGL explorer
cp system/kernel.app/modules/explorer_lvgl.py system/kernel.app/modules/explorer.py

# Step 3: Run kernel
micropython run.py
```

That's it! The module auto-detects LVGL availability and uses appropriate mode.

## What You Get

### In LVGL Mode (if LVGL available):
```
┌─────────────────────────────────────────────┐
│  [*] Files          ← desktop icon          │
│                                             │
│         ┌──────────────────┐                │
│         │ Explorer         │ [x]  ← window  │
│         ├──────────────────┤                │
│         │ [F] boot.py      │                │
│         │ [F] main.py      │                │
│         │ [F] device.json  │                │
│         └──────────────────┘                │
│                                             │
│ ═════════════════════════════════════════   ← taskbar
│ [:) Start]                       W B 12:34  │
└─────────────────────────────────────────────┘
```
:0 = when start is selected
### In Minimal Mode (LVGL unavailable):
```
Black screen showing:
Key: UP
```

## Keyboard

| Key | Action |
|-----|--------|
| ↑/↓ | Navigate |
| Return/Enter | SELECT (open file) |
| Esc | BACK (close) |
| ←/→ | Switch panes |

## Local Testing

```bash
# Terminal mode (no GUI)
micropython run.py

# With GUI emulator
python3 emulator.py &
micropython run.py

# LVGL layout only (requires lv_bindings)
python3 lvgl_test.py
```

## Customization

All in `lvgl_layout.py`:

```python
# Change colors
COLOR_DESKTOP = lv.color_hex(0x008080)  # Teal → Your color

# Add desktop icons
for icon in ['Files', 'Notepad']:
    # Create button in icon_cont

# Modify file list
for fname in ['file1.py', 'file2.py']:
    # Create list item in content area
```

## Architecture

```
NanoCore (kernel)
├── input module → publishes 'input.key'
├── explorer module (explorer_lvgl.py)
│   ├── LVGL mode: full desktop UI
│   └── minimal mode: key display
└── display module ← publishes 'display'
```

## Troubleshooting

| Issue | Fix |
|-------|-----|
| "LVGL unavailable" | Normal. Minimal mode active. LVGL is optional. |
| "failed to import lvgl modules" | Check `lvgl_layout.py` exists in modules/ |
| No keyboard response | Check emulator keyboard bindings in emulator.py |
| Module crashes | Try minimal mode: restore original explorer.py |

## Module Lifecycle

```python
explorer = ExplorerModule(core)
await explorer.run()
    ├─ _check_lvgl()           → True/False
    ├─ _run_lvgl() OR _run_minimal()
    │  ├─ _heartbeat()         → core.beat('explorer') every 2s
    │  ├─ _key_loop()          → listens to input.key
    │  ├─ _clock_loop()        → updates clock every 60s
    │  ├─ _tray_loop()         → listens to explorer.tray
    │  └─ _draw_loop_lvgl()    → lv.refr_now() every 100ms
    └─ Handles graceful shutdown (all tasks canceled)
```

## IPC Channels

### Input
- **`input.key`** — `{'key': 'UP', 'event': 'press'}`
- **`explorer.tray`** — `{'wifi': True}` (future)

### Output
- **`display`** — `{'type': 'raw', 'ops': [...]}`  (minimal mode only)

## Next Steps

1. **Test it** — Run `micropython run.py` and press keys in emulator
2. **Customize** — Edit colors/layout in `lvgl_layout.py`
3. **Enhance** — Implement file browser, Start menu, etc.
4. **Deploy** — Flash to ESP32 with LVGL-enabled MicroPython

## Links

- Full docs: [LVGL_LAYOUT.md](LVGL_LAYOUT.md)
- Integration: [LVGL_INTEGRATION.md](LVGL_INTEGRATION.md)
- PURR OS SDK: [docs.md](docs.md)
