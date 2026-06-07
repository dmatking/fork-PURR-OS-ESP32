# Lua 5.4 Component for PURR OS

PURR OS integrates Lua 5.4 as a lightweight scripting runtime for user apps.

## Setup

Before building, download the Lua 5.4 source:

### Windows
```powershell
cd CoreOS/components/lua
.\setup.ps1
```

### macOS / Linux
```bash
cd CoreOS/components/lua
chmod +x setup.sh
./setup.sh
```

This downloads Lua 5.4.6 from https://www.lua.org/ and extracts the C source into `src/`.

## What's Included

- **`lua_runtime.h/cpp`** — High-level Lua environment
  - `lua_runtime_init()` — Initialize Lua with KITT API bindings
  - `lua_run_file(path, restricted)` — Execute a .lua or .luac file
  - `lua_run_code(code, name, restricted)` — Execute Lua code as a string

- **Module registrations:**
  - `display.*` — Fill rectangles, draw text, clear screen
  - `sd.*` — Read/write files to SD card or SPIFFS
  - `touch.*` — Poll capacitive touch events
  - `system.*` — Time, delays, printing (restricted mode hides privileged APIs)

## Usage

### Basic Lua App

```lua
-- /apps/myapp/main.lua
display.clear()
display.text(10, 10, "Hello PURR OS!", 0xFFFF, 0x0000, 2)

for i = 1, 100 do
    display.fill_rect(i * 3, 50, 2, 100, 0xF800)  -- red bars
    system.delay(10)
end
```

### Reading from SD Card

```lua
local config = sd.read("/config.json")
if config then
    system.print(config)
end
```

### Touch Input

```lua
while true do
    local ev = touch.get_event()
    if ev then
        system.print("Touch at: " .. ev.x .. ", " .. ev.y .. "\n")
    end
    system.delay(50)
end
```

## Restriction Levels

- **`.paws` (Portable App With Sandbox):** Restricted access
  - Can read/write SD, access display, read touch
  - Cannot access flash partitions, WiFi, BT
  - `system.*` functions available but limited

- **`.claw` (Core Library And Widget):** Full access
  - All KITT APIs available
  - Can manipulate flash partitions
  - Can launch other apps

## Future Enhancements

- Lua bytecode compilation (.luac) for smaller app size
- Additional modules: `wifi.*`, `mqtt.*`, `mesh.*`
- Coroutine support for background tasks
- JIT compilation (LuaJIT) for performance-critical apps

## Performance

- Typical Lua app: 20-50 KB bytecode
- Interpreter overhead: ~100 KB in binary size
- Startup time: < 100ms for typical app
- Runtime: ~equivalent to MicroPython, 2-3x slower than native C

## Resources

- Lua 5.4 Manual: https://www.lua.org/manual/5.4/
- Lua C API: https://www.lua.org/pil/24.html
- PURR OS KITT API: See `CoreOS/system/kernel/kitt.h`
