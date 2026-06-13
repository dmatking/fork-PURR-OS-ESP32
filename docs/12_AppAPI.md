# 12 — App API & Unified Windowing

PURR OS apps never call LVGL or MiniWin directly. All UI goes through a single unified API — `purr_win.h` — which dispatches to whichever UI module is active at runtime.

---

## Layer Stack

```
App code   (terminal.c, calculator.c, your_app.c)
    │  #include "purr_win.h"
    │  purr_win_create(), purr_win_button(), purr_win_textarea() ...
    ▼
catcall_ui_t  ← registered by whichever UI module loaded first
    │
    ├── kittenui_win.c  (LVGL backend — small screens ≤320×240)
    └── miniwin_win.c   (MiniWin backend — large screens 480×320+)
    │
    ▼  catcall_display_t.push_pixels()
Display driver (.purr)
    ▼
Physical screen
```

Catcalls are the **hardware** layer (display pixels, touch points, radio packets). The app API is the **UI widget** layer above them. Apps sit above both.

---

## Choosing a Tier

| Tier | File ext | Gets `purr_win.h` | Gets kernel API | Use for |
|------|----------|-------------------|-----------------|---------|
| `.meow` | Lua script | via Lua bindings | No | Scripted tools, games |
| `.paws` | Compiled C | Yes | No | Calculators, viewers, utilities |
| `.claw` | Compiled C | Yes | Yes | Terminal, system tools, emulators |

Set `tier` in `app.pcat`. The build system enforces it.

---

## Quick Start

```c
#include "purr_win.h"   // that's it — no LVGL, no MiniWin

static purr_win_t win;

void my_app_init(void) {
    win = purr_win_create("My App");

    purr_wid_t lbl = purr_win_label(win, "Hello, PURR OS!");
    purr_win_label_align(lbl, PURR_ALIGN_CENTER);

    purr_wid_t btn = purr_win_button(win, "Tap me", on_tap, NULL);

    purr_win_show(win);
}

static void on_tap(purr_wid_t wid, purr_event_t event, void *user) {
    purr_win_label_set(lbl, "Tapped!");
}
```

---

## API Reference

### Windows

```c
purr_win_t purr_win_create(const char *title);
void       purr_win_show   (purr_win_t win);
void       purr_win_hide   (purr_win_t win);
void       purr_win_clear  (purr_win_t win);  // remove all child widgets
void       purr_win_destroy(purr_win_t win);
```

### Labels

```c
purr_wid_t purr_win_label      (purr_win_t win, const char *text);
void       purr_win_label_set  (purr_wid_t wid, const char *text);
void       purr_win_label_align(purr_wid_t wid, purr_align_t align);
// align: PURR_ALIGN_LEFT | PURR_ALIGN_CENTER | PURR_ALIGN_RIGHT
```

### Buttons

```c
purr_wid_t purr_win_button       (purr_win_t win, const char *label,
                                   purr_win_cb_t cb, void *user);
void       purr_win_button_enable(purr_wid_t wid, bool enabled);
```

Callback signature:
```c
typedef void (*purr_win_cb_t)(purr_wid_t wid, purr_event_t event, void *user);
// event: PURR_EVENT_CLICKED | PURR_EVENT_CHANGED | PURR_EVENT_FOCUSED
```

### Textarea

```c
purr_wid_t   purr_win_textarea          (purr_win_t win, uint16_t w_pct, uint16_t h_pct);
void         purr_win_textarea_append   (purr_wid_t wid, const char *text);
void         purr_win_textarea_set      (purr_wid_t wid, const char *text);
void         purr_win_textarea_clear    (purr_wid_t wid);
const char  *purr_win_textarea_get      (purr_wid_t wid);   // backend-owned, copy if needed
void         purr_win_textarea_focus    (purr_wid_t wid);   // show keyboard / cursor
void         purr_win_textarea_on_change(purr_wid_t wid, purr_win_cb_t cb, void *user);
```

`w_pct` and `h_pct` are percentages of the window content area (0–100).

### Layout

```c
purr_wid_t purr_win_row       (purr_win_t win, uint8_t padding);
purr_wid_t purr_win_col       (purr_win_t win, uint8_t padding);
void       purr_win_layout_end(purr_wid_t container);
```

Widgets created after `purr_win_row()` / `purr_win_col()` and before `purr_win_layout_end()` are placed inside that container. On KittenUI this uses LVGL flex layout. On MiniWin it uses simple vertical stacking.

### Keyboard (on-screen)

```c
void purr_win_keyboard_show(purr_win_t win, purr_wid_t target_textarea);
void purr_win_keyboard_hide(purr_win_t win);
```

On KittenUI: shows LVGL's built-in keyboard attached to the target textarea.  
On MiniWin: no-op — physical keyboard is handled via `catcall_input` automatically.

---

## Writing a Backend (for new UI modules)

If you're writing a new UI module (e.g. `oled_ui_win.c` for the text-mode OLED), implement `catcall_ui_t` and call `purr_kernel_register_ui()` from your module's `init()`:

```c
#include "catcall_ui.h"
#include "purr_kernel.h"

static const catcall_ui_t s_my_ui = {
    .name            = "my_ui",
    .catcall_version = CATCALL_UI_VERSION,
    .win_create      = my_win_create,
    .win_destroy     = my_win_destroy,
    // ... all function pointers
};

// Call from your module init():
purr_kernel_register_ui(&s_my_ui);
```

Any function pointer left NULL is a graceful no-op — `purr_win.h` checks before calling.

---

## Built-in System Apps

| App | Tier | File | Description |
|-----|------|------|-------------|
| terminal | `.claw` | `apps/system/terminal/` | Shell: ls, cat, echo, modules, mem, uptime, reboot |
| calculator | `.paws` | `apps/system/calculator/` | Basic arithmetic with decimal support |
| settings | `.claw` | `apps/system/settings/` | Theme, brightness, SD status, system reboot |
| about | `.claw` | `apps/system/about/` | OS/KITT version, chip info, free RAM, uptime, active drivers |
| fileman | `.claw` | `apps/system/fileman/` | Browse SPIFFS and SD card; preview text files |

`settings` and `about` are **staple** apps — always present. The rest follow the same unified API layer and can be excluded from a build if flash space is tight.

---

## File Manager Notes

The file manager (`fileman`) uses a two-panel layout:

```
[ SPIFFS ] [ SD ] [ Up ]
/spiffs
┌──────────────────┬────────────────┐
│ [dir/]           │ file preview   │
│  file.txt        │ (text content) │
│  config.json     │                │
└──────────────────┴────────────────┘
[ < Prev ]  [ Open ]  [ Next > ]
Status: [1/5] config.json
```

Use **Prev** / **Next** to cycle the selection cursor, **Open** to enter a directory or preview a file. Binary files are displayed with non-printable bytes replaced by `.`.

---

## Files

| File | Purpose |
|------|---------|
| `source/kernel/catcalls/catcall_ui.h` | Widget catcall contract — implement this for a new UI backend |
| `source/kernel/catcalls/purr_win.h` | App-facing unified API — include this in your app |
| `source/modules/kittenui/kittenui_win.c` | LVGL backend (small screens) |
| `source/modules/miniwin/miniwin_win.c` | MiniWin backend (large screens) |
| `source/apps/system/terminal/terminal.c` | Terminal app source |
| `source/apps/system/calculator/calculator.c` | Calculator app source |
| `source/apps/system/settings/settings.c` | Settings app source |
| `source/apps/system/about/about.c` | About screen source |
| `source/apps/system/fileman/fileman.c` | File manager source |
