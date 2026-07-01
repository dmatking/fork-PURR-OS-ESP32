# RGB565 color constants for PURR OS display modules.
# Compatible with SSD1306 (any non-zero value = on).
# On color displays (ILI9341/ILI9342C/ILI9488) the full RGB565 value is used.

BLACK      = 0x0000
WHITE      = 0xFFFF
RED        = 0xF800
GREEN      = 0x07E0
BLUE       = 0x001F
YELLOW     = 0xFFE0
CYAN       = 0x07FF
MAGENTA    = 0xF81F
ORANGE     = 0xFC00

# Grayscale
LIGHT_GRAY = 0xC618  # ~#C0C0C0  Windows classic silver
GRAY       = 0x8410  # ~#808080
DARK_GRAY  = 0x4208  # ~#404040
VERY_DARK  = 0x2104  # ~#202020

# UI accents
WIN_TEAL   = 0x0410  # #008080  Windows CE taskbar accent
WIN_BLUE   = 0x000F  # #000078  dark title-bar blue
