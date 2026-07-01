# MiniWin Shell

---

## Shell themes

Two shell themes are available. Both use MiniWin as the window manager — the theme controls only the shell chrome (root window paint + dock/menu behaviour).

### WCE Classic (`PURR_UI_THEME=wce`)

Windows CE-inspired gray UI. 22px taskbar at the bottom.

- **Meow! button** (Start): opens two-level menu — Programs → catalog apps, Restart
- **Taskbar**: app buttons between Start and RAM clock; tap to focus/restore minimised windows
- **RAM clock**: right side, shows free heap in kB

### Blackberry (`PURR_UI_THEME=blackberry`)

Green-on-black phosphor terminal theme.

```
 Y=0    ████ STATUS (16px) ████████████████████  PURR OS 0.9.4   NET:UP
 Y=16   ░░░░ TIME (24px) ░░░░░░░░░░░  00:05:22   device-name
 Y=40   ▓▓▓▓ NOTIF (12px) ▓▓▓▓▓▓▓  RAM 218 kB free
 Y=52   ░ ░ ░ ░ WALLPAPER (142px) ░ ░ ░ ░ ░ ░ ░
        ░ ░ ░ ░  [ PURR OS ]  ░ ░ ░ ░ ░ ░ ░ ░ ░
        ░ ░ ░ ░  tap to open apps  ░ ░ ░ ░ ░ ░ ░
 Y=194  ████ TABS (14px) ████  Recent │ All │ System
 Y=208  ████ DOCK (32px) ████  APPS │ FILES │ SETT │ BOOT
```

- **Wallpaper tap** → opens app drawer (4-column grid of catalog + SD apps)
- **APPS dock button** → same
- **Drawer tap on app** → launches it, closes drawer
- **Drawer close bar** → closes drawer
- **BOOT dock button** → `esp_restart()`
- SD apps show with red-tinted icons if `.claw` (admin)

---

## App window model

All app windows are created with `mw_add_window()`. The shell's root window sits behind all app windows.

```cpp
mw_util_rect_t r;
mw_util_set_rect(&r, APP_WIN_X(300), 14, 300, 200);
mw_handle_t h = mw_add_window(&r, "My App",
    paint_fn, message_fn,
    NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
```

Key macros from `purr_apps_common.h`:

```cpp
// Window flag presets
APP_WIN_FLAGS         // title bar + close + visible + fixed size
APP_WIN_FLAGS_TOUCH   // same + touch events routed to this window

// Center a window of width w horizontally
APP_WIN_X(w)          // → (display_width - w) / 2

// WCE color palette
WCE_BAR   0xC0C0C0   // taskbar gray
WCE_HI    0xFFFFFF   // highlight (top-left border edge)
WCE_SHD   0x808080   // shadow (inner bottom-right)
WCE_DARK  0x404040   // dark (outer bottom-right border)
WCE_TXT   0x000000   // text
```

---

## Touch coordinates

`MW_TOUCH_DOWN_MESSAGE` delivers coordinates **already in client space**:

```cpp
void my_message(const mw_message_t *msg) {
    if (msg->message_id != MW_TOUCH_DOWN_MESSAGE) return;
    int16_t cx = (int16_t)(msg->message_data >> 16);
    int16_t cy = (int16_t)(msg->message_data & 0xFFFF);
    // cx/cy are relative to the top-left of the client area
}
```

Do **not** subtract `mw_get_window_client_rect().x/y` — that causes a double-offset bug.

---

## Taskbar API

The WCE shell (and any shell that implements a taskbar) uses `purr_taskbar`:

```cpp
#include "purr_taskbar.h"

taskbar_register(handle, "My App");     // adds to taskbar row
taskbar_unregister(handle);             // removes (call from MW_WINDOW_REMOVED_MESSAGE)
taskbar_set_focus(handle);              // marks as active (visual highlight)

// Globals (read-only from app code)
extern taskbar_entry_t taskbar_entries[TASKBAR_MAX];
extern int             taskbar_entry_count;
extern mw_handle_t     taskbar_focused_handle;
```

---

## App catalog

The global app registry, read by both shell themes for their menus/drawers:

```cpp
#include "purr_app_catalog.h"

// Read-only access
extern const purr_catalog_entry_t purr_catalog[];  // name + launch()
extern const int purr_catalog_count;

// Launch an app by index
purr_catalog[idx].launch();
```

To add a new built-in app: add its `#include` and entry to `devices/apps/purr_app_catalog.cpp`. Wrap in `#ifdef PURR_HAS_YOURAPP` if it's optional.

---

## Timer-driven repaint

Most app windows use a `MW_TICKS_PER_SECOND`-based timer:

```cpp
case MW_WINDOW_CREATED_MESSAGE:
    mw_paint_window_client(msg->recipient_handle);
    mw_set_timer(MW_TICKS_PER_SECOND, msg->recipient_handle, MW_WINDOW_MESSAGE);
    break;
case MW_TIMER_MESSAGE:
    mw_paint_window_client(msg->recipient_handle);
    mw_set_timer(MW_TICKS_PER_SECOND, msg->recipient_handle, MW_WINDOW_MESSAGE);
    break;
```

For animated apps (e.g. Lua windows), use `MW_TICKS_PER_SECOND / 10` for ~10 fps.

---

## Writing a new shell theme

1. Create `devices/apps/shell_yourtheme.cpp` guarded by `#ifdef PURR_THEME_YOURTHEME`
2. Implement `extern "C" void mw_user_init()`, `mw_user_root_paint_function()`, `mw_user_root_message_function()`
3. Wrap existing `devices/*/purr_app.cpp` WCE content in `#ifndef PURR_THEME_YOURTHEME`
4. Add `PURR_THEME_YOURTHEME` define to `lib_miniwin/CMakeLists.txt` and `main/CMakeLists.txt`
5. Add theme to `UI_THEMES` in `SDK/sdk_core.py`

See `shell_blackberry.cpp` as a reference.
