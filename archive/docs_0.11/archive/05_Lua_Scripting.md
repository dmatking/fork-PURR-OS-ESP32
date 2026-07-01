# Lua Scripting

PURR OS runs Lua 5.4 scripts from the SD card. Scripts open in their own MiniWin window with a retained-mode widget system.

---

## Script types

| Extension | Type | APIs available |
|-----------|------|----------------|
| `.paws` | Userland (sandboxed) | `win.*`, `sd.*` |
| `.claw` | Admin (full access) | `win.*`, `sd.*`, `kitt.*` |

Place scripts in `/sdcard/apps/`. They appear automatically in the Apps launcher and shell app drawers.

---

## win.* — Window UI API

Builds a retained-mode widget list. Widgets are rendered by the MiniWin paint callback on each frame.

```lua
win.clear()                                  -- remove all widgets

-- Add a text label
win.label(x, y, "Hello world", color)
-- color is optional RGB888 uint (default white)

-- Add a button (draws WCE-style 3D raised button)
win.button(x, y, w, h, "Click me", color)

-- Add a filled rectangle
win.rect(x, y, w, h, color)

-- Wait for a touch event (blocks Lua task until touched)
-- Returns client-relative x, y
local x, y = win.wait_touch()

-- Sleep (non-blocking to WM, yields Lua task)
win.sleep(ms)

-- Query window client dimensions
local w = win.width()
local h = win.height()
```

### Simple counter example

```lua
-- counter.paws
local count = 0

while true do
    win.clear()
    win.label(10, 10, "PURR Counter", 0x00FF44)
    win.label(10, 40, "Count: " .. count, 0xFFFFFF)
    win.button(60, 80, 80, 30, "Tap me!", 0x002200)

    local x, y = win.wait_touch()
    if y >= 80 and y <= 110 then
        count = count + 1
    end
end
```

---

## sd.* — SD card file API

Available to both `.paws` and `.claw`.

```lua
-- Read entire file as string (returns nil, errmsg on failure)
local content, err = sd.read("/sdcard/data.txt")
if not content then print(err) end

-- Write string to file (returns true/false)
local ok = sd.write("/sdcard/out.txt", "hello\n")

-- List directory (returns table of {name, is_dir, size})
local entries = sd.list("/sdcard/")
for _, e in ipairs(entries) do
    print(e.name, e.is_dir, e.size)
end
```

---

## kitt.* — Admin API  (`.claw` only)

```lua
-- Network
local ok = kitt.wifi_connected()      -- bool

-- Storage
local ok = kitt.sd_available()        -- bool

-- System
kitt.reboot()                          -- trigger esp_restart()
local kb = kitt.free_ram()             -- free heap in kB
local ms = kitt.time_ms()             -- uptime in milliseconds
```

---

## Script lifecycle

Each script gets:
- Its own `lua_State*` (isolated — scripts cannot see each other's globals)
- Its own FreeRTOS task (runs independently of the WM task)
- A mutex protecting the widget list between the Lua task and the paint callback

The script runs until:
- It returns normally (window stays open showing last widget state)
- It calls `kitt.reboot()` (device restarts)
- A Lua error occurs (window shows error message)
- The user closes the window (task is killed, `lua_State` freed)

---

## Global Lua runtime (KITT-level)

Separate from the per-window Lua system above. Used for KITT-level automation:

```cpp
#include "lua_runtime.h"

lua_runtime_init();
lua_run_file("/sdcard/startup.lua", false);   // false = unrestricted
lua_run_code("kitt.log('hello')", "inline", true);
lua_runtime_deinit();
```

This runtime provides: `display.*`, `sd.*`, `touch.*`, `system.*` modules. It shares one `lua_State` for the lifetime of the device.

---

## Widget limits

| Resource | Limit |
|----------|-------|
| Widgets per window | 48 |
| Concurrent Lua windows | Unlimited (heap-limited in practice) |
| Script path length | 255 bytes |
| Label / button text | 64 bytes |

---

## Deploying scripts

```
/sdcard/apps/
  hello.paws         ← userland, no kitt.*
  sysinfo.claw       ← admin, full kitt.*
  mylib.lua          ← plain Lua (not auto-launched, loadable via sd.read)
```

Scripts do not need to be recompiled for different CYD variants — the Lua bytecode is device-agnostic.
