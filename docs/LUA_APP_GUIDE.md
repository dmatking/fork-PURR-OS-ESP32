# PURR OS Lua App Guide

Apps on PURR OS are Lua 5.4 scripts stored on the SD card and launched from the WCE start menu. Each app runs inside its own MiniWin window with a retained-mode widget API.

---

## File types

| Extension | Sandbox | Extra APIs | Use for |
|-----------|---------|------------|---------|
| `.paw` | Restricted | `win`, `sd` | User apps |
| `.claw` | Elevated | `win`, `sd`, `kitt` | System/admin tools |

Place files in the **root of the SD card** (`/sdcard/`). Subdirectory launching is not yet supported. The `libs/` folder under `/sdcard/libs/` is a convention for shared Lua modules loaded with `dofile`.

---

## Minimal app skeleton

```lua
-- hello.paw
local W = win.width()
local H = win.height()

win.rect(0, 0, W, H, 0x1A1A2E)          -- background
win.label("Hello, PURR!", 10, 10, 0xFFFFFF)

while true do
    local t = win.wait_touch(5000)       -- wait up to 5 s for a tap
    if t then
        win.clear()
        win.rect(0, 0, W, H, 0x1A1A2E)
        win.label(string.format("tap %d,%d", t.x, t.y), 10, 10, 0x44FF88)
    end
end
```

The script runs in a FreeRTOS task. The **main loop pattern** is:

1. Build your widget list with `win.*` calls.
2. Call `win.wait_touch(ms)` to block until a tap or timeout.
3. React, update widgets, repeat.

---

## API reference

### `win` — window drawing

All coordinates are **client-relative** (0,0 = top-left of the app window, not the screen).

| Function | Returns | Description |
|----------|---------|-------------|
| `win.clear()` | — | Remove all widgets from the list |
| `win.rect(x, y, w, h [, color])` | — | Filled rectangle. Default color: `0xC0C0C0` |
| `win.label(text, x, y [, color])` | — | Text string. Default color: black |
| `win.button(text, x, y [, w, h, id])` | — | Raised 3D button. Default 64×18. `id` is ignored by the renderer but useful for your own hit-testing |
| `win.wait_touch([timeout_ms])` | `{x,y}` or `nil` | Block until a touch-down event. Default timeout 30 000 ms. Returns `nil` on timeout |
| `win.sleep(ms)` | — | FreeRTOS delay (does not block the display) |
| `win.width()` | `number` | Client area width in pixels |
| `win.height()` | `number` | Client area height in pixels |

**Widget limit:** 128 widgets per frame. Call `win.clear()` at the start of each redraw.

**Colors** are 16-bit RGB565 packed as a Lua integer (e.g. `0xF800` = red, `0x07E0` = green, `0x001F` = blue). You can also use 24-bit hex literals and the runtime will truncate — for clarity the demo apps use 24-bit constants.

---

### `sd` — SD card I/O

Available in both `.paw` and `.claw`.

| Function | Returns | Description |
|----------|---------|-------------|
| `sd.read(path)` | `string` or `nil, errmsg` | Read entire file as a string |
| `sd.write(path, data)` | `bool` | Write string to file (overwrites) |
| `sd.list(dir)` | `table` | Directory listing. Each entry: `{name, size, is_dir}` |

Paths must be absolute: `/sdcard/mydata.txt`.

---

### `kitt` — system APIs (.claw only)

| Function | Returns | Description |
|----------|---------|-------------|
| `kitt.wifi_connected()` | `bool` | Whether WiFi is associated |
| `kitt.sd_available()` | `bool` | Whether SD card is mounted |
| `kitt.free_ram()` | `number` | Free heap bytes (`MALLOC_CAP_8BIT`) |
| `kitt.time_ms()` | `number` | Milliseconds since boot |
| `kitt.reboot()` | — | Calls `esp_restart()` immediately |

---

## Drawing patterns

### Solid background + text

```lua
local BG = 0x0C0C0C
local FG = 0x00FF44

win.rect(0, 0, win.width(), win.height(), BG)
win.label("Status: OK", 4, 4, FG)
```

### Raised / sunken button look

The `win.button()` widget renders raised automatically. For custom 3D effects using `win.rect()`:

```lua
local function raised_rect(x, y, w, h, fill)
    win.rect(x,   y,   w,   h,   fill)      -- fill
    win.rect(x,   y,   w,   1,   0xFFFFFF)  -- top highlight
    win.rect(x,   y,   1,   h,   0xFFFFFF)  -- left highlight
    win.rect(x,   y+h-1, w, 1,   0x404040)  -- bottom shadow
    win.rect(x+w-1, y,  1,  h,   0x404040)  -- right shadow
end

local function sunken_rect(x, y, w, h, fill)
    win.rect(x,   y,   w,   h,   fill)
    win.rect(x,   y,   w,   1,   0x404040)  -- top dark
    win.rect(x,   y,   1,   h,   0x404040)
    win.rect(x,   y+h-1, w, 1,   0xFFFFFF)  -- bottom light
    win.rect(x+w-1, y,  1,  h,   0xFFFFFF)
end
```

### Hit-testing a button grid

```lua
local buttons = {
    {x=4,  y=40, w=60, h=24, label="OK"},
    {x=68, y=40, w=60, h=24, label="Cancel"},
}

local function draw()
    win.clear()
    win.rect(0, 0, win.width(), win.height(), 0xC0C0C0)
    for _, b in ipairs(buttons) do
        win.button(b.label, b.x, b.y, b.w, b.h)
    end
end

draw()

while true do
    local t = win.wait_touch(10000)
    if t then
        for _, b in ipairs(buttons) do
            if t.x >= b.x and t.x < b.x + b.w and
               t.y >= b.y and t.y < b.y + b.h then
                -- b.label was tapped
            end
        end
        draw()
    end
end
```

### Loading a shared library

```lua
local kb = dofile("/sdcard/libs/keyboard.lua")
local name = kb.input("Enter name:", "")
```

`dofile` runs in the same Lua state, so the library has access to `win`, `sd`, and (for `.claw`) `kitt`.

---

## Uptime / timers

There is no Lua timer callback. Use `kitt.time_ms()` with `win.wait_touch(short_ms)` to fake a tick loop:

```lua
local last = kitt.time_ms()

while true do
    local t = win.wait_touch(100)   -- 100 ms tick
    local now = kitt.time_ms()
    local dt = now - last
    last = now

    -- physics / animation update using dt
    -- ...

    -- redraw
    win.clear()
    -- ...
end
```

---

## Resource limits

| Resource | Limit | Notes |
|----------|-------|-------|
| Widgets per frame | 128 | Call `win.clear()` each redraw |
| Widget label text | 47 chars | Truncated silently |
| Stack | 8 KB | Per Lua task (`xTaskCreate`) |
| Heap | Internal or PSRAM | PSRAM allocator used automatically if `PURR_HAS_PSRAM` is set |
| SD path length | 511 chars | |

---

## `.claw` system app notes

`.claw` files are identical in structure to `.paw` but get the `kitt` table registered. The WCE shell sets `is_admin = true` for any file with the `.claw` extension; no code change is needed in the script itself.

Use `.claw` for:
- Kernel terminals and diagnostic tools
- WiFi configuration
- Partition management
- Anything that needs `kitt.reboot()`

---

## Example apps

| File | Type | Description |
|------|------|-------------|
| `sdcard_apps/calc.paw` | `.paw` | 4-function calculator |
| `sdcard_apps/clock.paw` | `.paw` | 7-segment uptime clock + stopwatch |
| `sdcard_apps/bouncy.paw` | `.paw` | Bouncing shapes screensaver |
| `sdcard_apps/minesweeper.paw` | `.paw` | 6×6 minesweeper with hold-to-flag |
| `sdcard_apps/terminal.claw` | `.claw` | Live RAM/WiFi/SD stats + system log |
| `sdcard_apps/libs/keyboard.lua` | library | On-screen keyboard (`kb.input(prompt)`) |
