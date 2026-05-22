# PURR OS Emulator — Keyboard Control Guide

The emulator now has **full keyboard support** for the Windows CE UI.

## Running the Emulator

```bash
# Terminal 1: Start the emulator
python3 emulator.py

# Terminal 2: Start the kernel
micropython run.py
```

You'll see two windows open:
- **Emulator window**: Displays the PURR OS UI (320×240 display)
- **Terminal**: Shows kernel boot messages and shell prompt

## Keyboard Controls

**Click the emulator window to give it focus**, then use these keys:

| Key | Action |
|-----|--------|
| **Enter** or **Space** | Toggle Start menu open/close |
| **↑ Up arrow** | Navigate menu up |
| **↓ Down arrow** | Navigate menu down |
| **Esc** or **Backspace** | Close menu |
| **→ Left/Right arrows** | (Reserved for future use) |

## Status Indicator

The emulator's status bar shows:
- **● kernel connected** — System is ready for input
- **→ [KEY]** — Key press detected and sent (briefly highlights in green)
- **○ kernel disconnected** — Kernel has disconnected

## Testing the UI

1. **Start both processes** (emulator + kernel)
2. **Click the emulator window** to give it focus
3. **Press Enter** → Start menu appears with 5 items
4. **Press ↑/↓** → Menu selection highlight moves
5. **Press Esc** → Menu closes, returns to desktop
6. **Press Enter again** → Menu reopens

## Architecture

Keyboard events flow:
```
emulator.py (key press)
    ↓ [TCP socket]
run.py (reader thread receives JSON)
    ↓ [_emu_mod.injected_keys list]
input.py (_poll_loop drains queue)
    ↓ [IPC publish to input.key channel]
explorer.py (_key_loop processes events)
    ↓ [Updates menu state & redraws]
```

## Colors (Classic Windows CE 2.x Silver Theme)

- **Taskbar**: Silver (#C0C0C0)
- **Start Button**: Teal (#008080) with 3D beveled edges
- **Desktop**: Teal background
- **Menu**: Silver with blue selection highlight
- **Text**: Black on silver, white on dark backgrounds
