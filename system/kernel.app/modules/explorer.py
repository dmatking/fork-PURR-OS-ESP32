import uasyncio as asyncio
import utime

_BEAT_INTERVAL = 2000
_REFRESH_MS    = 1000
_TASKBAR_H     = 32

# Classic Windows CE 2.x silver palette
_DESKTOP  = 0x0410  # WIN_TEAL desktop
_TASKBAR  = 0xC618  # LIGHT_GRAY silver taskbar
_BTN_FACE = 0xC618  # LIGHT_GRAY button face
_HI       = 0xFFFF  # WHITE   bevel top/left highlight
_SH       = 0x4208  # DARK_GRAY bevel bottom/right shadow
_START    = 0x0410  # WIN_TEAL Start button
_SEL      = 0x000F  # WIN_BLUE selected item
_FG_DARK  = 0x0000  # BLACK text on silver
_FG_LIGHT = 0xFFFF  # WHITE text on dark backgrounds



def _raised(ops, x, y, w, h, face):
    """Draw a raised 3D button rectangle."""
    ops.append({'cmd': 'fill_rect', 'x': x+1, 'y': y+1, 'w': w-2, 'h': h-2, 'color': face})
    ops.append({'cmd': 'hline', 'x': x,     'y': y,     'w': w,   'color': _HI})
    ops.append({'cmd': 'vline', 'x': x,     'y': y,     'h': h,   'color': _HI})
    ops.append({'cmd': 'hline', 'x': x,     'y': y+h-1, 'w': w,   'color': _SH})
    ops.append({'cmd': 'vline', 'x': x+w-1, 'y': y,     'h': h,   'color': _SH})


def _pressed(ops, x, y, w, h, face):
    """Draw a pressed (sunken) 3D button rectangle."""
    ops.append({'cmd': 'fill_rect', 'x': x+1, 'y': y+1, 'w': w-2, 'h': h-2, 'color': face})
    ops.append({'cmd': 'hline', 'x': x,     'y': y,     'w': w,   'color': _SH})
    ops.append({'cmd': 'vline', 'x': x,     'y': y,     'h': h,   'color': _SH})
    ops.append({'cmd': 'hline', 'x': x,     'y': y+h-1, 'w': w,   'color': _HI})
    ops.append({'cmd': 'vline', 'x': x+w-1, 'y': y,     'h': h,   'color': _HI})


class ExplorerModule:
    NAME = 'explorer'

    def __init__(self, core):
        self._core  = core
        self._tray  = core.subscribe('explorer.tray')
        self._wifi  = False
        self._need_desktop = True
        self._w     = 320
        self._h     = 240
        self._scale = 2
        self._load_cfg()

    def _load_cfg(self):
        try:
            import json
            with open('/system/kernel.app/device.json') as f:
                cfg = json.load(f)
            res = cfg.get('display_res', [128, 64])
            self._w, self._h = res[0], res[1]
            self._scale = 2 if self._w >= 200 else 1
        except Exception:
            pass

    async def run(self):
        beat_task = asyncio.create_task(self._heartbeat())
        tray_task = asyncio.create_task(self._tray_loop())
        try:
            await self._clock_loop()
        finally:
            beat_task.cancel()
            tray_task.cancel()

    # ------------------------------------------------------------------

    async def _clock_loop(self):
        while True:
            self._draw()
            await asyncio.sleep_ms(_REFRESH_MS)

    async def _tray_loop(self):
        while True:
            msg = await self._tray.get()
            if msg.get('wifi') is not None:
                self._wifi = bool(msg['wifi'])
                self._draw()

    # ------------------------------------------------------------------

    def _draw(self):
        ops = []
        w   = self._w
        h   = self._h
        sc  = self._scale
        cw  = 8 * sc
        ch  = 8 * sc
        y_bar   = h - _TASKBAR_H       # 208 on 240p
        btn_y   = y_bar + 3
        btn_h   = _TASKBAR_H - 6       # 26
        ty      = y_bar + (_TASKBAR_H - ch) // 2   # vertically-centred text row

        # Desktop fill (only when needed — avoids stomping app content)
        if self._need_desktop:
            ops.append({'cmd': 'fill_rect', 'x': 0, 'y': 0,
                        'w': w, 'h': y_bar, 'color': _DESKTOP})
            self._need_desktop = False

        # Taskbar
        self._taskbar_ops(ops, w, sc, cw, ch, y_bar, btn_y, btn_h, ty)

        ops.append({'cmd': 'show'})
        self._core.publish('display', {'type': 'raw', 'ops': ops})

    # ------------------------------------------------------------------
    # Taskbar
    # ------------------------------------------------------------------

    def _taskbar_ops(self, ops, w, sc, cw, ch, y_bar, btn_y, btn_h, ty):
        # Silver background + top highlight
        ops.append({'cmd': 'fill_rect', 'x': 0, 'y': y_bar, 'w': w, 'h': _TASKBAR_H, 'color': _TASKBAR})
        ops.append({'cmd': 'hline',     'x': 0, 'y': y_bar, 'w': w, 'color': _HI})

        # --- Start button ---
        btn_w = 5*cw + 8           # "Start" 5 chars + 4px each side
        txt_x = 2 + (btn_w - 5*cw) // 2
        _raised(ops, 2, btn_y, btn_w, btn_h, _START)
        ops.append({'cmd': 'text', 's': 'Start', 'x': txt_x, 'y': ty,
                    'color': _FG_LIGHT, 'bg': _START})

        # Double-groove separator after Start
        sep1 = 2 + btn_w + 3
        ops.append({'cmd': 'vline', 'x': sep1,   'y': btn_y, 'h': btn_h, 'color': _SH})
        ops.append({'cmd': 'vline', 'x': sep1+1, 'y': btn_y, 'h': btn_h, 'color': _HI})

        # --- App slots (center, empty placeholder) ---
        # reserved: sep1+2 to tray_sep-2

        # --- System tray (right-to-left) ---
        s   = utime.ticks_ms() // 1000
        clk = '{:02d}:{:02d}'.format((s // 60) % 60, s % 60)
        wifi_s = 'W+' if self._wifi else 'W-'

        clk_x  = w - 5*cw - 4              # 236 on 320p
        wifi_x = clk_x - 2*cw - 6          # 214

        # Double-groove separator before tray
        tray_sep = wifi_x - 5
        ops.append({'cmd': 'vline', 'x': tray_sep,   'y': btn_y, 'h': btn_h, 'color': _SH})
        ops.append({'cmd': 'vline', 'x': tray_sep+1, 'y': btn_y, 'h': btn_h, 'color': _HI})

        # Tray contents
        ops.append({'cmd': 'text', 's': wifi_s, 'x': wifi_x, 'y': ty,
                    'color': _FG_DARK, 'bg': _TASKBAR})
        ops.append({'cmd': 'text', 's': clk,    'x': clk_x,  'y': ty,
                    'color': _FG_DARK, 'bg': _TASKBAR})

    # ------------------------------------------------------------------

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
