-- libs/keyboard.lua — On-screen keyboard for PURR OS Lua apps
-- Usage:
--   local kb = dofile("/sdcard/libs/keyboard.lua")
--   local text = kb.input("Enter name:", "default text")
--   -- returns the string, or nil if user tapped Cancel

local M = {}

-- Colors
local C_BG      = 0xD4D0C8
local C_DISP    = 0xFFFFFF
local C_BORDER  = 0x808080
local C_KEY_UP  = 0xC0C0C0
local C_KEY_DN  = 0xA0A0A0
local C_KEY_HI  = 0xFFFFFF
local C_KEY_SHD = 0x404040
local C_KEY_SPC = 0xB0C8E8  -- spacebar tint
local C_KEY_ACT = 0x0070AA  -- shift/caps active
local C_TXT     = 0x000000
local C_PROMPT  = 0x555555
local C_CURSOR  = 0x0055AA

local PAD     = 2
local KEY_H   = 22

-- Keyboard rows (lowercase / uppercase toggled by shift)
local ROWS_LO = {
    {"q","w","e","r","t","y","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l"},
    {"⇧","z","x","c","v","b","n","m","⌫"},
    {"123","  SPACE  ",".","OK"},
}
local ROWS_HI = {
    {"Q","W","E","R","T","Y","U","I","O","P"},
    {"A","S","D","F","G","H","J","K","L"},
    {"⇧","Z","X","C","V","B","N","M","⌫"},
    {"123","  SPACE  ",".","OK"},
}
local ROWS_NUM = {
    {"1","2","3","4","5","6","7","8","9","0"},
    {"-","/",":",";","(",")","$","&","@","\""},
    {"⇧",".",",","?","!","'","#","%","⌫"},
    {"ABC","  SPACE  ","+","OK"},
}

-- Shared state
local shifted  = false
local num_mode = false
local blink_t  = 0

local function active_rows()
    if num_mode then return ROWS_NUM end
    return shifted and ROWS_HI or ROWS_LO
end

local function build_layout(W, kb_y)
    -- Returns list of {x,y,w,h,label} for every key
    local layout = {}
    local rows = active_rows()
    local y = kb_y
    for ri, row in ipairs(rows) do
        -- Measure total natural width for this row
        local natural = 0
        for _, lbl in ipairs(row) do
            -- wider keys
            if lbl == "  SPACE  " then natural = natural + 90
            elseif lbl == "⇧" or lbl == "⌫" then natural = natural + 32
            elseif lbl == "OK" then natural = natural + 36
            elseif #lbl > 2 then natural = natural + 36
            else natural = natural + 26
            end
            natural = natural + PAD
        end
        -- Scale to fit window
        local scale = (W - PAD*2) / natural
        local x = PAD
        for _, lbl in ipairs(row) do
            local nat
            if lbl == "  SPACE  " then nat = 90
            elseif lbl == "⇧" or lbl == "⌫" then nat = 32
            elseif lbl == "OK" then nat = 36
            elseif #lbl > 2 then nat = 36
            else nat = 26
            end
            local kw = math.floor(nat * scale)
            layout[#layout+1] = {x=x, y=y, w=kw, h=KEY_H, label=lbl, row=ri}
            x = x + kw + PAD
        end
        y = y + KEY_H + PAD
    end
    return layout
end

local function draw_key(k, pressed)
    local bg = pressed and C_KEY_DN or C_KEY_UP
    -- Special key tints
    if k.label == "OK" then
        bg = pressed and 0x005588 or 0x0070AA
    elseif k.label == "  SPACE  " then
        bg = pressed and 0x9AB8D4 or C_KEY_SPC
    elseif k.label == "⇧" then
        bg = (shifted or num_mode) and C_KEY_ACT or (pressed and C_KEY_DN or C_KEY_UP)
    elseif k.label == "⌫" then
        bg = pressed and 0x662222 or 0x884444
    end

    win.rect(k.x, k.y, k.w, k.h, bg)
    if pressed then
        win.rect(k.x, k.y, k.w, 1, C_KEY_SHD)
        win.rect(k.x, k.y, 1, k.h, C_KEY_SHD)
        win.rect(k.x, k.y+k.h-1, k.w, 1, C_KEY_HI)
        win.rect(k.x+k.w-1, k.y, 1, k.h, C_KEY_HI)
    else
        win.rect(k.x, k.y, k.w, 1, C_KEY_HI)
        win.rect(k.x, k.y, 1, k.h, C_KEY_HI)
        win.rect(k.x, k.y+k.h-1, k.w, 1, C_KEY_SHD)
        win.rect(k.x+k.w-1, k.y, 1, k.h, C_KEY_SHD)
    end

    -- Label
    local disp = k.label == "  SPACE  " and "" or k.label
    if #disp > 0 then
        local lx = k.x + math.floor((k.w - #disp*6) / 2)
        local ly = k.y + math.floor((k.h - 8) / 2)
        local fg = (k.label == "OK") and C_KEY_HI or C_TXT
        win.label(disp, lx, ly, fg)
    end
end

local function draw_display(prompt, text, W, disp_h)
    win.rect(0, 0, W, disp_h, C_BG)
    -- Prompt
    win.label(prompt, 4, 4, C_PROMPT)
    -- Input box
    local box_y = 18
    local box_h = disp_h - box_y - 4
    win.rect(4, box_y, W-8, box_h, C_DISP)
    win.rect(4, box_y, W-8, 1, C_BORDER)
    win.rect(4, box_y, 1, box_h, C_BORDER)
    win.rect(4, box_y+box_h-1, W-8, 1, C_BORDER)
    win.rect(W-4, box_y, 1, box_h, C_BORDER)
    -- Text (right-clip if too long)
    local max_chars = math.floor((W-16) / 6)
    local visible = text
    if #visible > max_chars then
        visible = visible:sub(#visible - max_chars + 1)
    end
    win.label(visible, 8, box_y + math.floor((box_h-8)/2), C_TXT)
    -- Cursor blink
    local cx = 8 + #visible * 6
    local blink_on = math.floor(kitt.time_ms() / 500) % 2 == 0
    if blink_on then
        win.rect(cx, box_y + 3, 2, box_h - 6, C_CURSOR)
    end
end

function M.input(prompt, default)
    prompt  = prompt  or ""
    default = default or ""
    local text = default

    local W      = win.width()
    local H      = win.height()
    local DISP_H = 42
    local kb_y   = DISP_H + 2

    shifted  = false
    num_mode = false

    local layout    = build_layout(W, kb_y)
    local last_rows = shifted  -- detect layout change

    local function redraw(pressed_lbl)
        win.clear()
        win.rect(0, 0, W, H, C_BG)
        draw_display(prompt, text, W, DISP_H)
        -- Rebuild layout if mode changed
        if shifted ~= last_rows or num_mode then
            layout    = build_layout(W, kb_y)
            last_rows = shifted
        end
        for _, k in ipairs(layout) do
            draw_key(k, k.label == pressed_lbl)
        end
    end

    redraw(nil)

    while true do
        local t = win.wait_touch(400)

        if t then
            -- Find tapped key
            for _, k in ipairs(layout) do
                if t.x >= k.x and t.x < k.x+k.w and
                   t.y >= k.y and t.y < k.y+k.h then

                    redraw(k.label)  -- visual feedback

                    local lbl = k.label
                    if lbl == "OK" then
                        return text
                    elseif lbl == "⌫" then
                        if #text > 0 then text = text:sub(1,-2) end
                    elseif lbl == "⇧" then
                        if num_mode then
                            num_mode = false
                            shifted  = false
                        else
                            shifted = not shifted
                        end
                        layout = build_layout(W, kb_y)
                    elseif lbl == "123" or lbl == "ABC" then
                        num_mode = not num_mode
                        shifted  = false
                        layout   = build_layout(W, kb_y)
                    elseif lbl == "  SPACE  " then
                        text = text .. " "
                        if shifted then shifted = false end
                    else
                        text = text .. lbl
                        -- Auto-unshift after one capital
                        if shifted and not num_mode then shifted = false end
                        layout = build_layout(W, kb_y)
                    end
                    break
                end
            end
        end

        redraw(nil)
    end
end

return M
