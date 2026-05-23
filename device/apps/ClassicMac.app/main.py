"""
PURR OS — Mac System 7/8 desktop shell (explorer.app).

Menu bar: PURR | Apps | [active app] | W L
Desktop: clean black, no icons — all control through the menu bar.

Keys:
  BACK   — open PURR menu (About, System Info, WiFi, Quit PURR OS)
  SELECT — open Apps menu
"""

import uasyncio as asyncio
import utime

_BEAT_INTERVAL = 2000
_REFRESH_MS    = 100

# Mac System 4/5 palette — monochrome-inspired (RGB565)
_MB_BG     = 0xFFFF   # menu bar — white
_MB_TXT    = 0x0000   # text / borders — black
_DESK      = 0xC618   # desktop — classic Mac light gray
_WIN_BG    = 0xFFFF   # window / dropdown background — white
_DROP_SEL  = 0x0000   # selection — inverted black (System 5 style)
_SEP       = 0x8410   # separators / dim text — gray
_TOAST_BG  = 0xFFFF
_TOAST_BDR = 0x0000

_MENUBAR_H = 20       # slightly thicker bar
_WIN_TH    = 14
_ROW_H     = 14


class Module:
    NAME = 'classicmac'

    def __init__(self, core):
        self._core      = core
        self._keys      = core.subscribe('input.key')
        self._tray_q    = core.subscribe('explorer.tray')
        self._notify_q  = core.subscribe('explorer.notify')
        self._launch_q  = core.subscribe('system.app.launching')
        self._stopped_q = core.subscribe('system.app.stopped')

        self._w           = 320
        self._h           = 240
        self._small_mode  = False
        self._device_name = 'purr_os'
        self._disp_name   = 'unknown'
        self._flash       = '?'

        # Menu bar state
        self._purr_open  = False
        self._apps_open  = False
        self._menu_sel   = 0
        self._menu_cache = []
        self._active_app = ''     # shown in center of menu bar
        self._child      = None   # running child app; None = we own the display

        # Screens
        self._about_mode   = False
        self._sysinfo_mode = False
        self._wifi_mode    = False

        # Tray
        self._wifi_on = False
        self._lora_on = False

        # Toasts
        self._toasts = []

        # WiFi screen
        self._wifi_nets   = []
        self._wifi_cursor = 0

        self._load_cfg()
        self._calc_layout()

    def _load_cfg(self):
        try:
            import json
            with open('/system/kernel.app/device.json') as f:
                cfg = json.load(f)
            res = cfg.get('display_res', [128, 64])
            self._w, self._h  = res[0], res[1]
            self._device_name = cfg.get('device', 'purr_os')
            self._disp_name   = cfg.get('display', 'unknown')
            self._flash       = cfg.get('flash', '?')
        except Exception:
            pass

    def _calc_layout(self):
        self._small_mode = self._h < 100
        self._menu_w     = max(140, int(self._w * 0.55))

    # ── App scanning ──────────────────────────────────────────────────────────

    def _scan_apps(self):
        import os
        apps = []
        try:
            for entry in sorted(os.listdir('/apps')):
                if not entry.endswith('.app'):
                    continue
                name = entry[:-4]
                if name in ('explorer',):
                    continue
                try:
                    os.stat('/apps/{}/main.py'.format(entry))
                    apps.append(name)
                except OSError:
                    pass
        except OSError:
            pass
        return apps

    @staticmethod
    def _purr_items():
        return ['About This ESP32', '---', 'System Info', '---', 'WiFi',
                '---', 'Quit PURR OS']

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    async def run(self):
        beat_task   = asyncio.create_task(self._heartbeat())
        key_task    = asyncio.create_task(self._key_loop())
        tray_task   = asyncio.create_task(self._tray_loop())
        notify_task = asyncio.create_task(self._notify_loop())
        track_task  = asyncio.create_task(self._app_track_loop())
        child_task  = asyncio.create_task(self._child_watch_loop())
        try:
            await self._draw_loop()
        finally:
            for t in (beat_task, key_task, tray_task, notify_task, track_task, child_task):
                t.cancel()

    async def _app_track_loop(self):
        while True:
            msg = await self._launch_q.get()
            name = msg.get('name', '')
            if name and name not in ('explorer', 'classicmac'):
                self._active_app = name
                self._child = name

    async def _child_watch_loop(self):
        while True:
            msg = await self._stopped_q.get()
            if msg.get('name') == self._child:
                self._child = None
                self._active_app = ''

    async def _key_loop(self):
        while True:
            msg   = await self._keys.get()
            if self._child:
                continue   # child owns the keys
            key   = msg.get('key')
            event = msg.get('event')
            if not (key and event == 'press'):
                continue
            if self._purr_open:
                self._handle_key_menu(key, self._purr_items())
            elif self._apps_open:
                self._handle_key_menu(key, self._menu_cache)
            elif self._about_mode or self._sysinfo_mode:
                self._handle_key_info(key)
            elif self._wifi_mode:
                self._handle_key_wifi(key)
            else:
                self._handle_key_desktop(key)

    # ── Key handlers ─────────────────────────────────────────────────────────

    def _handle_key_desktop(self, key):
        if key in ('SELECT', 'DOWN', 'UP'):
            self._menu_cache = self._scan_apps()
            self._menu_sel   = 0
            self._apps_open  = True
        elif key == 'BACK':
            items = self._purr_items()
            self._menu_sel  = next(
                (i for i, x in enumerate(items) if not x.startswith('---')), 0
            )
            self._purr_open = True

    def _handle_key_menu(self, key, items):
        n = len(items)
        if key == 'DOWN':
            self._menu_sel = (self._menu_sel + 1) % n
            while items[self._menu_sel].startswith('---'):
                self._menu_sel = (self._menu_sel + 1) % n
        elif key == 'UP':
            self._menu_sel = (self._menu_sel - 1) % n
            while items[self._menu_sel].startswith('---'):
                self._menu_sel = (self._menu_sel - 1) % n
        elif key == 'SELECT':
            selected          = items[self._menu_sel] if items else None
            was_purr          = self._purr_open
            self._purr_open   = False
            self._apps_open   = False
            self._menu_sel    = 0
            if was_purr:
                self._on_purr_select(selected)
            elif selected:
                self._active_app = selected
                self._core.publish('system.app.launch', {'app': selected})
        elif key == 'BACK':
            self._purr_open = False
            self._apps_open = False
            self._menu_sel  = 0

    def _on_purr_select(self, item):
        if item == 'About This ESP32':
            self._about_mode = True
        elif item == 'System Info':
            self._sysinfo_mode = True
        elif item == 'WiFi':
            self._wifi_mode   = True
            self._wifi_cursor = 0
            self._wifi_nets   = []
            asyncio.create_task(self._wifi_scan())
        elif item == 'Quit PURR OS':
            self._core.publish('display', {'type': 'clear'})
            try:
                import machine
                machine.reset()
            except Exception:
                pass
        elif item:
            self._active_app = item
            self._core.publish('system.app.launch', {'app': item})

    def _handle_key_info(self, key):
        if key in ('BACK', 'SELECT'):
            self._about_mode   = False
            self._sysinfo_mode = False

    def _handle_key_wifi(self, key):
        if key == 'DOWN' and self._wifi_nets:
            self._wifi_cursor = (self._wifi_cursor + 1) % len(self._wifi_nets)
        elif key == 'UP' and self._wifi_nets:
            self._wifi_cursor = (self._wifi_cursor - 1) % len(self._wifi_nets)
        elif key == 'SELECT' and self._wifi_nets:
            ssid = self._wifi_nets[self._wifi_cursor]
            self._core.publish('wifi.connect', {'ssid': ssid})
            self._toast('WiFi: connecting...')
            self._wifi_mode = False
        elif key == 'BACK':
            self._wifi_mode = False

    # ── Toast helpers ─────────────────────────────────────────────────────────

    def _toast(self, text, ms=3000):
        expire = utime.ticks_add(utime.ticks_ms(), ms)
        self._toasts.append([text, expire])

    def _expire_toasts(self):
        now = utime.ticks_ms()
        self._toasts = [t for t in self._toasts
                        if utime.ticks_diff(t[1], now) > 0]

    # ── Draw loop ─────────────────────────────────────────────────────────────

    async def _draw_loop(self):
        while True:
            self._expire_toasts()
            if not self._child:
                self._draw()
            await asyncio.sleep_ms(_REFRESH_MS)

    def _draw(self):
        if self._small_mode:
            self._draw_small()
            return
        ops = []
        if self._purr_open:
            self._draw_desktop(ops)
            self._draw_dropdown(ops, self._purr_items(), self._menu_sel, 0, _MENUBAR_H)
        elif self._apps_open:
            self._draw_desktop(ops)
            self._draw_dropdown(ops, self._menu_cache, self._menu_sel, 40, _MENUBAR_H)
        elif self._about_mode:
            self._draw_about(ops)
        elif self._sysinfo_mode:
            self._draw_sysinfo(ops)
        elif self._wifi_mode:
            self._draw_wifi(ops)
        else:
            self._draw_desktop(ops)
        if self._toasts:
            self._draw_toasts(ops)
        ops.append({'cmd': 'show'})
        self._core.publish('display', {'type': 'raw', 'ops': ops})

    # ── Gfx helpers ───────────────────────────────────────────────────────────

    def _draw_menubar(self, ops):
        # White bar
        ops.append({'cmd': 'fill_rect', 'x': 0, 'y': 0,
                    'w': self._w, 'h': _MENUBAR_H, 'color': _MB_BG})
        ops.append({'cmd': 'hline', 'x': 0, 'y': _MENUBAR_H - 1,
                    'w': self._w, 'color': _MB_TXT})

        ty = (_MENUBAR_H - 8) // 2   # vertically centered text

        # PURR  Apps (left side) — invert when menu is open
        purr_fg = _MB_BG if self._purr_open else _MB_TXT
        apps_fg = _MB_BG if self._apps_open else _MB_TXT
        if self._purr_open:
            ops.append({'cmd': 'fill_rect', 'x': 2, 'y': 0,
                        'w': 36, 'h': _MENUBAR_H - 1, 'color': _MB_TXT})
        if self._apps_open:
            ops.append({'cmd': 'fill_rect', 'x': 42, 'y': 0,
                        'w': 36, 'h': _MENUBAR_H - 1, 'color': _MB_TXT})
        ops.append({'cmd': 'text', 's': 'PURR', 'x': 4,  'y': ty, 'color': purr_fg})
        ops.append({'cmd': 'text', 's': 'Apps', 'x': 44, 'y': ty, 'color': apps_fg})

        # Status icons (right side) — W L, no clock
        wc = _MB_TXT if self._wifi_on else _SEP
        lc = _MB_TXT if self._lora_on else _SEP
        ops.append({'cmd': 'text', 's': 'L', 'x': self._w - 12, 'y': ty, 'color': lc})
        ops.append({'cmd': 'text', 's': 'W', 'x': self._w - 24, 'y': ty, 'color': wc})

        # Active app name — right after Apps, separated by a dim pipe
        if self._active_app:
            ops.append({'cmd': 'text', 's': '|', 'x': 84, 'y': ty, 'color': _SEP})
            label  = self._active_app
            max_ch = max(1, (self._w - 106) // 8)   # leave room for W L
            ops.append({'cmd': 'text', 's': label[:max_ch],
                        'x': 94, 'y': ty, 'color': _MB_TXT})

    def _draw_mac_window(self, ops, title, x, y, w, h):
        # Fill and outer border
        ops.append({'cmd': 'fill_rect', 'x': x, 'y': y, 'w': w, 'h': h, 'color': _WIN_BG})
        ops.append({'cmd': 'hline', 'x': x,     'y': y,     'w': w, 'color': _MB_TXT})
        ops.append({'cmd': 'vline', 'x': x,     'y': y,     'h': h, 'color': _MB_TXT})
        ops.append({'cmd': 'hline', 'x': x,     'y': y+h-1, 'w': w, 'color': _MB_TXT})
        ops.append({'cmd': 'vline', 'x': x+w-1, 'y': y,     'h': h, 'color': _MB_TXT})

        # Title bar — white base + horizontal stripes (System 4/5 candy-stripe look)
        ops.append({'cmd': 'fill_rect', 'x': x+1, 'y': y+1,
                    'w': w-2, 'h': _WIN_TH, 'color': _WIN_BG})
        for sy in range(y + 2, y + 1 + _WIN_TH, 2):
            ops.append({'cmd': 'hline', 'x': x+1, 'y': sy, 'w': w-2, 'color': _MB_TXT})
        ops.append({'cmd': 'hline', 'x': x+1, 'y': y+_WIN_TH, 'w': w-2, 'color': _MB_TXT})

        # Close box (left square)
        bx, by = x + 4, y + 3
        ops.append({'cmd': 'fill_rect', 'x': bx,   'y': by,   'w': 9, 'h': 9, 'color': _WIN_BG})
        ops.append({'cmd': 'hline',     'x': bx,   'y': by,   'w': 9, 'color': _MB_TXT})
        ops.append({'cmd': 'vline',     'x': bx,   'y': by,   'h': 9, 'color': _MB_TXT})
        ops.append({'cmd': 'hline',     'x': bx,   'y': by+8, 'w': 9, 'color': _MB_TXT})
        ops.append({'cmd': 'vline',     'x': bx+8, 'y': by,   'h': 9, 'color': _MB_TXT})

        # Zoom box (right square — System 5)
        zx, zy = x + w - 13, y + 3
        ops.append({'cmd': 'fill_rect', 'x': zx,   'y': zy,   'w': 9, 'h': 9, 'color': _WIN_BG})
        ops.append({'cmd': 'hline',     'x': zx,   'y': zy,   'w': 9, 'color': _MB_TXT})
        ops.append({'cmd': 'vline',     'x': zx,   'y': zy,   'h': 9, 'color': _MB_TXT})
        ops.append({'cmd': 'hline',     'x': zx,   'y': zy+8, 'w': 9, 'color': _MB_TXT})
        ops.append({'cmd': 'vline',     'x': zx+8, 'y': zy,   'h': 9, 'color': _MB_TXT})

        # Title text — punched out of stripes in a white clearing
        max_ch = max(1, (w - 46) // 8)   # leave room for close + zoom boxes
        tstr   = title[:max_ch]
        tx     = x + max(18, (w - len(tstr) * 8) // 2)
        ops.append({'cmd': 'fill_rect', 'x': tx - 4,       'y': y + 2,
                    'w': len(tstr) * 8 + 8, 'h': _WIN_TH - 2, 'color': _WIN_BG})
        ops.append({'cmd': 'text', 's': tstr, 'x': tx, 'y': y + 3, 'color': _MB_TXT})
        return x + 2, y + _WIN_TH + 1, w - 4, h - _WIN_TH - 3

    def _draw_dropdown(self, ops, items, selected, ax, ay):
        row_h   = _ROW_H
        total_h = sum(5 if i.startswith('---') else row_h for i in items) + 4
        w       = self._menu_w
        if ay + total_h > self._h:
            ay = max(0, self._h - total_h)
        if ax + w > self._w:
            ax = max(0, self._w - w)
        ops.append({'cmd': 'fill_rect', 'x': ax, 'y': ay,
                    'w': w, 'h': total_h, 'color': _WIN_BG})
        ops.append({'cmd': 'hline', 'x': ax,     'y': ay,           'w': w, 'color': _MB_TXT})
        ops.append({'cmd': 'vline', 'x': ax,     'y': ay,           'h': total_h, 'color': _MB_TXT})
        ops.append({'cmd': 'hline', 'x': ax,     'y': ay+total_h-1, 'w': w, 'color': _MB_TXT})
        ops.append({'cmd': 'vline', 'x': ax+w-1, 'y': ay,           'h': total_h, 'color': _MB_TXT})
        iy = ay + 2
        for idx, item in enumerate(items):
            if item.startswith('---'):
                ops.append({'cmd': 'hline', 'x': ax+4, 'y': iy+2,
                            'w': w-8, 'color': _SEP})
                iy += 5
                continue
            if idx == selected:
                ops.append({'cmd': 'fill_rect', 'x': ax+2, 'y': iy,
                            'w': w-4, 'h': row_h-1, 'color': _DROP_SEL})
                ops.append({'cmd': 'text', 's': item[:(w-8)//8],
                            'x': ax+6, 'y': iy+3, 'color': _WIN_BG})
            else:
                ops.append({'cmd': 'text', 's': item[:(w-8)//8],
                            'x': ax+6, 'y': iy+3, 'color': _MB_TXT})
            iy += row_h

    def _draw_toasts(self, ops):
        tx = self._w - 162
        ty = _MENUBAR_H + 2
        for text, _ in self._toasts[-3:]:
            ops.append({'cmd': 'fill_rect', 'x': tx-2, 'y': ty,
                        'w': 156, 'h': 14, 'color': _TOAST_BG})
            ops.append({'cmd': 'hline', 'x': tx-2, 'y': ty,    'w': 156, 'color': _TOAST_BDR})
            ops.append({'cmd': 'hline', 'x': tx-2, 'y': ty+13, 'w': 156, 'color': _TOAST_BDR})
            ops.append({'cmd': 'vline', 'x': tx-2, 'y': ty,    'h': 14,  'color': _TOAST_BDR})
            ops.append({'cmd': 'vline', 'x': tx+153,'y': ty,   'h': 14,  'color': _TOAST_BDR})
            ops.append({'cmd': 'text',  's': text[:18],
                        'x': tx, 'y': ty+3, 'color': _TOAST_BDR})
            ty += 16

    # ── Screen draw methods ───────────────────────────────────────────────────

    def _draw_desktop(self, ops):
        ops.append({'cmd': 'fill', 'color': _DESK})
        self._draw_menubar(ops)

    def _draw_about(self, ops):
        ops.append({'cmd': 'fill', 'color': _DESK})
        self._draw_menubar(ops)
        ww = min(240, self._w - 20)
        wh = 106
        wx = (self._w - ww) // 2
        wy = _MENUBAR_H + (self._h - _MENUBAR_H - wh) // 2
        cx, cy, cw, _ = self._draw_mac_window(ops, 'About This ESP32', wx, wy, ww, wh)
        for i, line in enumerate([
            'PURR OS 1.0',
            self._device_name,
            '{} {}x{}'.format(self._disp_name, self._w, self._h),
            'Flash: ' + self._flash,
            '',
            'SELECT or BACK to close',
        ]):
            ops.append({'cmd': 'text', 's': line[:cw//8],
                        'x': cx + 4, 'y': cy + i * 13, 'color': _MB_TXT})

    def _draw_sysinfo(self, ops):
        ops.append({'cmd': 'fill', 'color': _DESK})
        self._draw_menubar(ops)
        ww = min(240, self._w - 20)
        wh = 106
        wx = (self._w - ww) // 2
        wy = _MENUBAR_H + (self._h - _MENUBAR_H - wh) // 2
        cx, cy, cw, _ = self._draw_mac_window(ops, 'System Info', wx, wy, ww, wh)
        try:
            import gc
            free    = gc.mem_free()
            total   = free + gc.mem_alloc()
            mem_str = 'RAM: {}K free / {}K'.format(free // 1024, total // 1024)
        except Exception:
            mem_str = 'RAM: ?'
        try:
            import machine
            freq_str = 'CPU: {}MHz'.format(machine.freq() // 1_000_000)
        except Exception:
            freq_str = 'CPU: ?'
        up     = utime.ticks_ms() // 1000
        h, r   = divmod(up, 3600)
        m, s   = divmod(r, 60)
        up_str = 'Up: {:d}:{:02d}:{:02d}'.format(h, m, s)
        for i, line in enumerate([mem_str, freq_str, up_str, '', 'SELECT or BACK to close']):
            ops.append({'cmd': 'text', 's': line[:cw//8],
                        'x': cx + 4, 'y': cy + i * 14, 'color': _MB_TXT})

    def _draw_wifi(self, ops):
        ops.append({'cmd': 'fill', 'color': _DESK})
        self._draw_menubar(ops)
        ww = min(220, self._w - 20)
        wh = 120
        wx = (self._w - ww) // 2
        wy = _MENUBAR_H + (self._h - _MENUBAR_H - wh) // 2
        cx, cy, cw, ch = self._draw_mac_window(ops, 'WiFi', wx, wy, ww, wh)
        if not self._wifi_nets:
            ops.append({'cmd': 'text', 's': 'Scanning...',
                        'x': cx + 4, 'y': cy + 4, 'color': _MB_TXT})
            ops.append({'cmd': 'text', 's': 'BACK to cancel',
                        'x': cx + 4, 'y': cy + 20, 'color': _SEP})
        else:
            rows = max(1, (ch - 14) // _ROW_H)
            for i, ssid in enumerate(self._wifi_nets[:rows]):
                iy     = cy + i * _ROW_H
                is_sel = i == self._wifi_cursor
                if is_sel:
                    ops.append({'cmd': 'fill_rect', 'x': cx, 'y': iy,
                                'w': cw, 'h': _ROW_H - 1, 'color': _DROP_SEL})
                    fg = _WIN_BG
                else:
                    fg = _MB_TXT
                ops.append({'cmd': 'text', 's': ssid[:cw//8],
                            'x': cx + 2, 'y': iy + 2, 'color': fg})
            sy = cy + ch - 12
            ops.append({'cmd': 'hline', 'x': cx, 'y': sy - 2, 'w': cw, 'color': _SEP})
            ops.append({'cmd': 'text', 's': 'SEL:connect  BACK:close',
                        'x': cx + 2, 'y': sy, 'color': _SEP})

    # ── Small mode (128×64) ───────────────────────────────────────────────────

    def _draw_small(self):
        lines = []
        bar = 'PURR OS'
        if self._active_app:
            bar += '  ' + self._active_app[:6]
        lines.append(bar[:16])
        if self._purr_open or self._apps_open:
            items   = self._purr_items() if self._purr_open else self._menu_cache
            visible = [x for x in items if not x.startswith('---')]
            for i, item in enumerate(visible[:5]):
                sel = self._menu_sel
                lines.append(('>' if i == sel else ' ') + item[:14])
            while len(lines) < 7:
                lines.append('')
            lines.append('SEL:open BCK:close')
        elif self._about_mode or self._sysinfo_mode:
            if self._about_mode:
                lines += ['PURR OS 1.0', self._device_name[:16],
                          '{}x{}'.format(self._w, self._h)[:16], self._flash[:16]]
            else:
                try:
                    import gc
                    lines.append('{}K free'.format(gc.mem_free() // 1024))
                except Exception:
                    lines.append('RAM: ?')
                lines.append('Up: {}s'.format(utime.ticks_ms() // 1000))
            while len(lines) < 7:
                lines.append('')
            lines.append('SEL/BCK:close')
        else:
            wl = ('W' if self._wifi_on else 'w') + (' L' if self._lora_on else ' l')
            lines.append(wl)
            while len(lines) < 7:
                lines.append('')
            lines.append('SEL:apps BCK:purr')
        self._core.publish('display', {'type': 'text', 'lines': lines[:8]})

    # ── Background tasks ──────────────────────────────────────────────────────

    async def _tray_loop(self):
        while True:
            msg = await self._tray_q.get()
            if 'wifi' in msg:
                self._wifi_on = bool(msg['wifi'])
            if 'lora' in msg:
                self._lora_on = bool(msg['lora'])

    async def _notify_loop(self):
        while True:
            msg  = await self._notify_q.get()
            text = msg.get('text', '')
            ms   = int(msg.get('ms', 3000))
            if text:
                self._toast(text, ms)

    async def _wifi_scan(self):
        try:
            import network
            wlan = network.WLAN(network.STA_IF)
            wlan.active(True)
            nets   = wlan.scan()
            result = []
            for n in nets:
                ssid = n[0]
                if isinstance(ssid, (bytes, bytearray)):
                    try:
                        ssid = ssid.decode()
                    except Exception:
                        ssid = str(ssid)
                if ssid:
                    result.append(ssid)
            self._wifi_nets = result if result else ['(no networks found)']
        except Exception:
            self._wifi_nets = ['(scan failed)']

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
