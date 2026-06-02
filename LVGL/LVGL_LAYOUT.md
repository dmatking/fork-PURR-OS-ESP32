# Windows CE LVGL Layout for PURR OS

A pure MicroPython implementation of a Windows CE inspired UI using LVGL (Lightweight and Versatile Graphics Library).

## Files

- **`system/kernel.app/modules/lvgl_layout.py`** — Core LVGL layout definitions
  - Pure LVGL/MicroPython code with no hardcoded coordinates
  - Flexbox-based responsive layout
  - Windows CE color palette (teal desktop, silver taskbar, dark blue titles)
  - Functions: `build_desktop(scr)`, `update_clock()`, `show_start_menu()`

- **`system/kernel.app/modules/explorer_lvgl.py`** — Kernel integration
  - Async module for PURR OS
  - Keyboard input handling (UP/DOWN/LEFT/RIGHT/SELECT/BACK)
  - Clock updates every minute
  - Fallback to minimal display if LVGL unavailable
  - Event callbacks for UI interactions

- **`lvgl_test.py`** — Standalone local development tester
  - Test the layout on your Mac without the full kernel
  - SDL2 backend for display
  - Interactive UI testing

## Installation

### For ESP32 (PURR OS)

LVGL must be available on your MicroPython build:

```bash
# Flash ESP32 with LVGL-enabled MicroPython
# (See your MicroPython documentation for LVGL bindings)
```

Add LVGL to your `device.json`:
```json
{
  "display": "ili9342",
  "display_res": [320, 240],
  "enable_lvgl": true
}
```

### For Local Testing (Mac)

```bash
pip3 install lv_bindings pygame
python3 lvgl_test.py
```

## Architecture

```
explorer_lvgl.ExplorerModule (kernel integration)
    ├── _init_lvgl()          — Initialize LVGL & build UI
    ├── _key_loop()           — Listen for keyboard input
    ├── _clock_loop()         — Update clock every minute
    ├── _draw_loop()          — Refresh display
    └── _run_minimal()        — Fallback if LVGL unavailable
        └── lvgl_layout.build_desktop(scr)
            ├── _build_desktop()      — Teal background + icon grid
            ├── _build_taskbar()      — Bottom bar with Start button & tray
            └── _build_explorer_window() — Floating file manager
```

## Layout Components

### Desktop Background
- Teal (`0x008080`) solid color
- Icon grid container (left side)
- Mock "Files" icon with directory symbol

### Taskbar (32px height, bottom edge)
- **Left:** Start button
  - Silver background (`0xC0C0C0`)
  - 3D beveled borders (light highlight, dark shadow)
- **Right:** System tray
  - WiFi icon
  - Bluetooth icon
  - Clock label (HH:MM format)

### Explorer Window (200×160px, centered)
- **Title bar:** Dark blue (`0x000080`) with white text
  - "Explorer" label (left)
  - Close button × (right)
- **Content area:** White background
  - Flex column layout
  - Mock file list items
  - Each file: icon + filename

## Color Palette

Windows CE 2.x silver theme:

```python
COLOR_DESKTOP    = 0x008080    # Teal background
COLOR_TASKBAR    = 0xC0C0C0    # Light gray buttons/bars
COLOR_TITLEBAR   = 0x000080    # Dark blue window titles
COLOR_HIGHLIGHT  = 0xFFFFFF    # White (bright highlights, text)
COLOR_SHADOW     = 0x808080    # Dark gray (3D shadows)
COLOR_TEXT       = 0x000000    # Black text
```

## Keyboard Input

The layout responds to keyboard input from the input module:

| Key | Action |
|-----|--------|
| `SELECT` (Return) | Activate button / Open file |
| `BACK` (Escape) | Close window / Go back |
| `UP` / `DOWN` | Navigate list |
| `LEFT` / `RIGHT` | Switch panes |

Mapped in emulator.py:
```python
_KEY_MAP = {
    'Return':    'SELECT',
    'Escape':    'BACK',
    'Up':        'UP',
    'Down':      'DOWN',
    'Left':      'LEFT',
    'Right':     'RIGHT',
}
```

## Usage

### Kernel Integration

Replace `explorer.py` with `explorer_lvgl.py`:

```bash
# Option 1: Replace the file
cp system/kernel.app/modules/explorer_lvgl.py system/kernel.app/modules/explorer.py

# Option 2: Update main.py to use the new module
# In main.py, change:
#   from modules.explorer import ExplorerModule
# to:
#   from modules.explorer_lvgl import ExplorerModule
```

### Local Testing

```bash
python3 lvgl_test.py
```

This opens a 320×240 window with the full Windows CE UI. Click buttons to test interactivity.

## Customization

### Add More Desktop Icons

In `lvgl_layout.py`, modify `_build_desktop()`:

```python
for icon in ['Files', 'Notepad', 'Calculator']:
    btn = lv.button(icon_cont)
    # ... set up button
```

### Extend the File List

In `lvgl_layout.py`, modify `_build_explorer_window()`:

```python
files = [
    {'name': 'boot.py', 'icon': 'f'},
    {'name': 'main.py', 'icon': 'f'},
    {'name': 'folder/', 'icon': 'd'},
]
for f in files:
    # ... create list item with dynamic data
```

### Change Color Scheme

Edit the color constants in both files:

```python
COLOR_DESKTOP = lv.color_hex(0x008080)  # Change to your preferred color
```

## Known Limitations

1. **LVGL must be available** — Falls back to minimal key display if not
2. **No touch input yet** — Only keyboard navigation supported
3. **Static file list** — Currently hardcoded mock files (TODO: Dynamic file browsing)
4. **No native dialogs** — Menu system is a placeholder (TODO: Implement Start menu)

## Future Enhancements

- [ ] Touch input support (via input module)
- [ ] Dynamic file browsing from actual filesystem
- [ ] Start menu with application launcher
- [ ] Notification area with system events (WiFi status, battery, etc.)
- [ ] Window management (minimize, maximize, drag)
- [ ] Themed window manager with multiple open apps

## Troubleshooting

### "LVGL not available" message
- Ensure your MicroPython build includes LVGL
- Check `device.json` has `"enable_lvgl": true`
- The fallback mode will still work for basic keyboard display

### Window rendering issues
- Verify display driver is initialized before explorer module starts
- Check `device.json` for correct `display_res` (should match actual hardware)
- Try reducing complexity: simplify `_build_explorer_window()` content

### Keyboard not responding
- Check input module is running: `core.register('input', InputModule, critical=True)`
- Verify keycodes match `_KEY_MAP` in emulator.py
- Check explorer module is actually subscribed to 'input.key' channel

## References

- LVGL Documentation: https://docs.lvgl.io/8.3/
- MicroPython LVGL Bindings: https://github.com/lvgl/lv_binding_micropython
- Windows CE UI Guidelines: https://docs.microsoft.com/en-us/windows/win32/ui-design-guidelines
