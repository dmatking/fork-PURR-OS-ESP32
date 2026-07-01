-- PURR OS Lua Test App
-- Simple test to verify Lua runtime and KITT API bindings

display.clear()
display.text(10, 10, "PURR OS Lua Runtime", 0xFFFF, 0x0000, 2)

-- Test system module
system.print("[lua] system.time_ms = " .. system.time_ms() .. "\n")

-- Test display
local colors = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF }
for i, color in ipairs(colors) do
    local x = (i - 1) * 50
    display.fill_rect(x, 40, 50, 20, color)
end

display.text(10, 70, "Color bars above", 0xC618, 0x0000, 1)

-- Test touch (poll for 2 seconds)
display.text(10, 100, "Touch screen...", 0xFFFF, 0x0000, 1)

local start_time = system.time_ms()
while system.time_ms() - start_time < 2000 do
    local ev = touch.get_event()
    if ev and ev.pressed then
        display.fill_rect(ev.x - 5, ev.y - 5, 10, 10, 0xFFFF)
    end
    system.delay(20)
end

-- Done
display.clear()
display.text(10, 50, "Lua Test Complete!", 0x07E0, 0x0000, 2)
display.text(10, 100, "Check serial log", 0x8410, 0x0000, 1)

system.print("[lua] Test app finished\n")
