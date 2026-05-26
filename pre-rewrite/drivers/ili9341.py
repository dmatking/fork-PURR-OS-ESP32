import machine
import utime
import framebuf

_SWRESET = const(0x01)
_SLPOUT  = const(0x11)
_NORON   = const(0x13)
_INVOFF  = const(0x20)
_DISPON  = const(0x29)
_CASET   = const(0x2A)
_RASET   = const(0x2B)
_RAMWR   = const(0x2C)
_MADCTL  = const(0x36)
_COLMOD  = const(0x3A)


class ILI9341:
    """
    SPI driver for ILI9341 / ILI9342C / ILI9488-compatible displays.
    Exposes the same fill/text/hline/fill_rect/show API as SSD1306 so the
    display module works unchanged across both display families.
    Color 0 = black, color 1 = white (maps to RGB565 0x0000 / 0xFFFF).
    """

    def __init__(self, spi, cs, dc, rst=None, bl=None, width=320, height=240, scale=2):
        self.spi    = spi
        self.cs     = cs
        self.dc     = dc
        self.rst    = rst
        self.width  = width
        self.height = height
        self.scale  = scale   # text scale factor; 2 = 16×16px chars on 320px display

        if rst:
            rst.value(0)
            utime.sleep_ms(100)
            rst.value(1)
            utime.sleep_ms(100)

        self._init_display()

        if bl:
            bl.value(1)

    # ------------------------------------------------------------------
    # Low-level SPI helpers
    # ------------------------------------------------------------------

    def _cmd(self, cmd, data=None):
        self.dc.value(0)
        self.cs.value(0)
        self.spi.write(bytes([cmd]))
        self.cs.value(1)
        if data is not None:
            self.dc.value(1)
            self.cs.value(0)
            self.spi.write(bytes(data))
            self.cs.value(1)

    def _window(self, x0, y0, x1, y1):
        """Set write window and leave bus in data-write state (CS low, DC high)."""
        self._cmd(_CASET, [x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF])
        self._cmd(_RASET, [y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF])
        self.dc.value(0)
        self.cs.value(0)
        self.spi.write(bytes([_RAMWR]))
        self.cs.value(1)
        self.dc.value(1)
        self.cs.value(0)
        # caller writes pixel data, then calls self.cs.value(1)

    def _init_display(self):
        self._cmd(_SWRESET)
        utime.sleep_ms(150)
        self._cmd(_SLPOUT)
        utime.sleep_ms(500)
        self._cmd(_COLMOD, [0x55])  # 16-bit RGB565
        self._cmd(_MADCTL,  [0x08]) # BGR bit only; 320×240 native for ILI9342C
        self._cmd(_INVOFF)
        self._cmd(_NORON)
        self._cmd(_DISPON)
        utime.sleep_ms(100)

    # ------------------------------------------------------------------
    # SSD1306-compatible drawing API
    # ------------------------------------------------------------------

    def _rgb565(self, color):
        if color == 0:  return (0x00, 0x00)
        if color == 1:  return (0xFF, 0xFF)
        return (color >> 8, color & 0xFF)  # raw RGB565

    def fill(self, color):
        hi, lo = self._rgb565(color)
        self._window(0, 0, self.width - 1, self.height - 1)
        chunk = bytes([hi, lo] * 128)
        total = self.width * self.height
        for _ in range(total // 128):
            self.spi.write(chunk)
        rem = total % 128
        if rem:
            self.spi.write(bytes([hi, lo] * rem))
        self.cs.value(1)

    def fill_rect(self, x, y, w, h, color):
        hi, lo = self._rgb565(color)
        self._window(x, y, x + w - 1, y + h - 1)
        total = w * h
        chunk = bytes([hi, lo] * min(128, total))
        for _ in range(total // 128):
            self.spi.write(chunk)
        rem = total % 128
        if rem:
            self.spi.write(bytes([hi, lo] * rem))
        self.cs.value(1)

    def hline(self, x, y, w, color):
        self.fill_rect(x, y, w, 1, color)

    def vline(self, x, y, h, color):
        self.fill_rect(x, y, 1, h, color)

    def text(self, s, x, y, color=1, bg=0):
        fg_hi, fg_lo = self._rgb565(color)
        bg_hi, bg_lo = self._rgb565(bg)
        sc = self.scale
        char_w = 8 * sc
        char_h = 8 * sc

        # Reuse buffers across characters to reduce GC pressure
        mono = bytearray(8)         # 8×8 MONO_HLSB = 8 bytes
        row_out = bytearray(char_w * 2)  # one pixel-row scaled horizontally

        for i, ch in enumerate(s):
            cx = x + i * char_w
            if cx + char_w > self.width:
                break

            fb = framebuf.FrameBuffer(mono, 8, 8, framebuf.MONO_HLSB)
            fb.fill(0)
            fb.text(ch, 0, 0, 1)

            self._window(cx, y, cx + char_w - 1, y + char_h - 1)
            for row in range(8):
                byte = mono[row]
                idx = 0
                for col in range(8):
                    # MONO_HLSB: bit 7 = leftmost pixel
                    p = (byte >> (7 - col)) & 1
                    bh = fg_hi if p else bg_hi
                    bl = fg_lo if p else bg_lo
                    for _ in range(sc):
                        row_out[idx]     = bh
                        row_out[idx + 1] = bl
                        idx += 2
                for _ in range(sc):
                    self.spi.write(row_out)
            self.cs.value(1)

    def show(self):
        pass  # direct GRAM writes — no framebuf to flush
