# PURR OS — Apps

## App Tiers

PURR OS has three app tiers, each with a distinct file extension and capability level.

| Extension | Name | API access | Typical use |
|-----------|------|-----------|-------------|
| `.meow` | Lua script | `win.*`, `sd.*`, `kitt.*` | Scripted tools, dashboards, simple games |
| `.paws` | Compiled userland | `win.*`, `sd.*` | Native apps with no direct kernel calls |
| `.claw` | Compiled kernel-access | Full `purr_kernel_*` + `win.*` + `sd.*` | System tools, emulators, advanced shells |

All three tiers access UI through the same `purr_win.h` dispatch layer (or `win.*` Lua bindings) — apps are not tied to a specific UI framework.

---

## Unified UI API — purr_win.h

Added in v0.12.0. All compiled apps (.paws and .claw) use `purr_win.h` instead of calling LVGL or MiniWin directly. The active UI module (KittenUI or MiniWin) registers a `catcall_ui_t` at boot and `purr_win.h` dispatches through it.

```c
#include "purr_win.h"   // all you need — no LVGL, no MiniWin headers

static purr_win_t  s_win;
static purr_wid_t  s_lbl;

static void on_tap(purr_wid_t w, purr_event_t e, void *u) {
    (void)w; (void)e; (void)u;
    purr_win_label_set(s_lbl, "Tapped!");
}

int my_app_init(void) {
    s_win = purr_win_create("My App");
    s_lbl = purr_win_label(s_win, "Hello, PURR OS!");
    purr_win_button(s_win, "Tap me", on_tap, NULL);
    purr_win_show(s_win);
    return 0;
}
```

This app runs identically on KittenUI (LVGL) and MiniWin without any changes.

### Window management

```c
purr_win_t purr_win_create (const char *title);
void       purr_win_show   (purr_win_t win);
void       purr_win_hide   (purr_win_t win);
void       purr_win_clear  (purr_win_t win);   // remove all child widgets
void       purr_win_destroy(purr_win_t win);
```

### Labels

```c
purr_wid_t purr_win_label      (purr_win_t win, const char *text);  // create + add
void       purr_win_label_set  (purr_wid_t wid, const char *text);  // update text
void       purr_win_label_align(purr_wid_t wid, purr_align_t align);
// align: PURR_ALIGN_LEFT | PURR_ALIGN_CENTER | PURR_ALIGN_RIGHT
```

### Buttons

```c
purr_wid_t purr_win_button       (purr_win_t win, const char *label,
                                   purr_win_cb_t cb, void *user_data);
void       purr_win_button_enable(purr_wid_t wid, bool enabled);
```

Callback signature:
```c
typedef void (*purr_win_cb_t)(purr_wid_t wid, purr_event_t event, void *user);
// event: PURR_EVENT_CLICKED | PURR_EVENT_CHANGED | PURR_EVENT_FOCUSED
```

### Textarea

```c
purr_wid_t  purr_win_textarea          (purr_win_t win, uint16_t w_pct, uint16_t h_pct);
void        purr_win_textarea_append   (purr_wid_t wid, const char *text);
void        purr_win_textarea_set      (purr_wid_t wid, const char *text);
void        purr_win_textarea_clear    (purr_wid_t wid);
const char *purr_win_textarea_get      (purr_wid_t wid);   // backend-owned, copy if needed
void        purr_win_textarea_focus    (purr_wid_t wid);   // shows keyboard / cursor
void        purr_win_textarea_on_change(purr_wid_t wid, purr_win_cb_t cb, void *user);
```

`w_pct` and `h_pct` are percentages of the window content area (0-100).

### Layout containers

```c
purr_wid_t purr_win_row       (purr_win_t win, uint8_t padding);  // horizontal row
purr_wid_t purr_win_col       (purr_win_t win, uint8_t padding);  // vertical column
void       purr_win_layout_end(purr_wid_t container);
```

Widgets created between `purr_win_row()` / `purr_win_col()` and `purr_win_layout_end()` are placed inside that container. On KittenUI this uses LVGL flex layout; on MiniWin it uses simple stacking.

### On-screen keyboard

```c
void purr_win_keyboard_show(purr_win_t win, purr_wid_t target_textarea);
void purr_win_keyboard_hide(purr_win_t win);
```

On KittenUI: shows LVGL's built-in keyboard. On MiniWin: no-op — physical keyboard handled via `catcall_input` automatically.

---

## .meow — Lua Scripts

`.meow` files are Lua 5.4 scripts run in a sandboxed VM by app_manager. The VM exposes three namespaces.

### `win.*` — Window API

```lua
win.title("My App")
win.clear(color)               -- fill window with RGB565 color
win.text(x, y, "Hello")
win.rect(x, y, w, h, color)
win.button(id, x, y, w, h, "label")
win.on_touch(function(x, y) end)
win.on_button(id, function() end)
win.show()
win.close()
```

### `sd.*` — SD Card API

```lua
local f = sd.open("myapp/data.txt", "r")
local contents = f:read("*a")
f:close()

local f2 = sd.open("myapp/out.txt", "w")
f2:write("hello world\n")
f2:close()

local files = sd.list("myapp/")
sd.mkdir("myapp/new_dir/")
local ok = sd.exists("myapp/data.txt")
```

### `kitt.*` — KITT Kernel API

```lua
-- Display
local w, h = kitt.display_size()

-- LoRa radio
kitt.radio_send("hello from PURR OS")
local msg = kitt.radio_recv()        -- nil if nothing waiting
local rssi = kitt.radio_rssi()

-- GPS
local fix = kitt.gps_fix()
if fix and fix.valid then
    print(fix.latitude, fix.longitude, fix.altitude_m, fix.satellites)
end

-- System info
print(kitt.free_ram())              -- bytes
print(kitt.uptime_ms())             -- milliseconds
kitt.reboot()
```

### Example .meow app

```lua
-- hello.meow

win.title("Hello PURR")
win.clear(0x0000)

local count = 0
win.button(1, 10, 50, 100, 30, "Tap me!")

win.on_button(1, function()
    count = count + 1
    win.clear(0x0000)
    win.text(10, 10, "Tapped: " .. count)
end)

win.text(10, 10, "Tapped: 0")
win.show()
```

---

## .paws — Compiled Userland Apps

`.paws` apps are compiled native binaries. They have access to `purr_win.h` and SD file APIs, but **no** `purr_kernel_*` calls.

### app.pcat

```ini
name        = "my_app"
version     = "0.1.0"
tier        = "paws"
author      = "Your Name"
description = "What this app does."

idf_requires = "esp_common driver freertos"
```

### Building

```bash
catstrap build my_app
# Output: cattobaked/apps/my_app.paws
# Also writes CMakeLists.txt into source/apps/*/my_app/ for IDF component inclusion
```

### Minimal .paws app

```c
#include "purr_win.h"
#include "purr_module.h"

static purr_win_t s_win;

static int my_init(void) {
    s_win = purr_win_create("My App");
    purr_win_label(s_win, "Hello!");
    purr_win_show(s_win);
    return 0;
}

static void my_deinit(void) {
    purr_win_destroy(s_win);
}

purr_module_header_t purr_module = {
    .magic         = PURR_MODULE_MAGIC,
    .abi_version   = PURR_MODULE_ABI_VERSION,
    .module_type   = PURR_MOD_APP,
    .load_priority = PURR_PRIORITY_OPTIONAL,
    .name          = "my_app",
    .version       = "0.1.0",
    .init          = my_init,
    .deinit        = my_deinit,
};
```

---

## .claw — Kernel-Access Apps

`.claw` apps have the same structure as `.paws` but add full `purr_kernel_*` access: catcall accessors, system info, reboot, and SD availability checks.

```c
#include "purr_win.h"
#include "purr_kernel.h"    // adds purr_kernel_display(), free_ram(), etc.
#include "purr_module.h"

// Access display info directly:
const catcall_display_t *disp = purr_kernel_display();
if (disp) {
    display_info_t info;
    disp->get_info(&info);
    // info.width, info.height, info.name
}

// Access radio:
const catcall_radio_t *radio = purr_kernel_radio();
if (radio) radio->send((uint8_t*)"ping", 4);

// System info:
uint32_t free_ram = purr_kernel_free_ram();
uint64_t uptime   = purr_kernel_uptime_ms();
bool     has_sd   = purr_kernel_sd_available();
```

Set `tier = "claw"` in app.pcat.

---

## Built-in System Apps

These are baked into the SPIFFS flash image for medium/large-screen devices:

| App | Tier | Description |
|-----|------|-------------|
| `settings` | `.claw` | Theme (WCE/Luna/Dark via NVS), brightness, SD status, reboot |
| `about` | `.claw` | OS + KITT version, chip, free RAM, uptime (live), active drivers |
| `terminal` | `.claw` | Shell: `ls`, `cat`, `echo`, `modules`, `mem`, `uptime`, `reboot` |
| `fileman` | `.claw` | Browse SPIFFS + SD; Prev/Next/Open navigation; text file preview |
| `calculator` | `.paws` | Basic arithmetic, decimal support, ERR:DIV0 guard |

`settings` and `about` are staple system features — always present on any medium/large screen device. The rest follow the same `purr_win.h` API and can be excluded on flash-constrained builds.

---

## App Scan Order

`app_manager` scans at boot in this priority order:

```
/flash/apps      baked-in system apps (highest priority)
/sdcard/apps     user-installed apps from SD card
```

SD apps with the same name as a flash app shadow the flash version — useful for testing app updates without reflashing.

---

## Writing Your First App

### Option A — .meow (fastest)

1. Create `myapp.meow` on the SD card at `/sdcard/apps/myapp.meow`
2. Write Lua using `win.*`, `sd.*`, `kitt.*`
3. Power on — appears in the Cat Apps launcher
4. Tap to launch

### Option B — .paws (native, no kernel)

1. `mkdir source/apps/user/myapp/`
2. Write `app.pcat` (tier = "paws") and `myapp.c`
3. `catstrap build myapp`
4. Copy `cattobaked/apps/myapp.paws` to `/sdcard/apps/`

### Option C — .claw (native, full kernel)

Same as .paws but set `tier = "claw"` in app.pcat and include `purr_kernel.h`.

---

## App Directory Layout

```
source/apps/
  system/             built-in system apps
    settings/         theme, brightness, SD, reboot
    about/            OS info, chip info, uptime, driver status
    terminal/         built-in shell
    fileman/          file manager
    calculator/       calculator
  exclusive/          in-house exclusives (rewrite in progress)
    magicmac/         Mac OS inspired shell
    magidos/          DOS inspired shell
```

User apps live on the SD card at `/sdcard/apps/` — they are never in the repo.
