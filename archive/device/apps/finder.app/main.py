"""
Finder — file browser app for PURR OS.

Launched via the Apps menu. Displays filesystem in a Mac-style window.

Keys:
  SELECT (first)  — move cursor down
  SELECT (second) — open item / enter directory
  DOWN            — move cursor down
  UP / BACK       — go up one directory (BACK at root = quit)
  ENTER           — open immediately
"""

import uasyncio as asyncio
import os
import utime

_BEAT_INTERVAL = 2000
_REFRESH_MS    = 80

# Palette (Mac System 7/8 RGB565)
_BG       = 0x0000   # desktop — black
_WIN_BG   = 0xFFFF   # window content — white
_WIN_TTL  = 0xCE59   # title bar — light gray
_BLACK    = 0x0000
_BLUE     = 0x001F   # selection highlight
_GRAY     = 0x8410   # dim / separators

_MENUBAR_H = 20   # matches Mac shell bar height
_WIN_TH    = 14
_ROW_H     = 14


def _fstat(path):
    try:
        st = os.stat(path)
        return bool(st[0] & 0x4000), st[6]
    except OSError:
        return False, 0

def _flistdir(path):
    try:
        entries = sorted(os.listdir(path))
    except OSError:
        return []
    dirs, files = [], []
    for e in entries:
        is_dir, _ = _fstat(_fjoin(path, e))
        (dirs if is_dir else files).append(e)
    return dirs + files

def _fjoin(base, name):
    return '/' + name if base == '/' else base.rstrip('/') + '/' + name

def _fsize(sz):
    if sz >= 1 << 20: return '{}M'.format(sz >> 20)
    if sz >= 1 << 10: return '{}K'.format(sz >> 10)
    return '{}B'.format(sz)


class Module:
    NAME = 'finder'

    def __init__(self, core):
        self._core = core
        self._keys = core.subscribe('input.key')

        self._w = 320
        self._h = 240

        self._path    = '/'
        self._items   = []
        self._stats   = {}
        self._cursor  = 0
        self._scroll  = 0
        self._confirm = False
        self._status  = ''

        self._small_mode = False
        self._load_cfg()

    def _load_cfg(self):
        try:
            import json
            with open('/system/kernel.app/device.json') as f:
                cfg = json.load(f)
            res = cfg.get('display_res', [128, 64])
            self._w, self._h = res[0], res[1]
        except Exception:
            pass
        self._small_mode = self._h < 100

    # ── Filesystem helpers ────────────────────────────────────────────────────

    def _refresh(self):
        raw = _flistdir(self._path)
        self._items  = ['..'] + raw if self._path != '/' else raw
        self._stats  = {'..': (True, 0)}
        for n in raw:
            self._stats[n] = _fstat(_fjoin(self._path, n))
        self._cursor = min(self._cursor, max(0, len(self._items) - 1))

    def _advance(self):
        if self._items:
            self._cursor = (self._cursor + 1) % len(self._items)
        self._confirm = True

    def _enter(self):
        name = self._items[self._cursor] if self._items else None
        if not name:
            return
        if name == '..':
            self._go_up()
            return
        full   = _fjoin(self._path, name)
        is_dir = self._stats.get(name, (False, 0))[0]
        if is_dir:
            self._path    = full
            self._cursor  = 0
            self._scroll  = 0
            self._refresh()
            self._status  = ''
        else:
            _, sz = self._stats.get(name, (False, 0))
            self._status = full + ' ' + _fsize(sz)
        self._confirm = False

    def _go_up(self):
        if self._path == '/':
            # Exit Finder back to explorer shell
            self._core.publish('system.app.stop', {'app': self.NAME})
            return
        self._path    = '/'.join(self._path.rstrip('/').split('/')[:-1]) or '/'
        self._cursor  = 0
        self._scroll  = 0
        self._confirm = False
        self._status  = ''
        self._refresh()

    def _visible(self, rows):
        c, s = self._cursor, self._scroll
        if c < s:
            self._scroll = c
        elif c >= s + rows:
            self._scroll = c - rows + 1
        s = self._scroll
        return s, self._items[s: s + rows]

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    async def run(self):
        self._refresh()
        beat_task = asyncio.create_task(self._heartbeat())
        key_task  = asyncio.create_task(self._key_loop())
        try:
            await self._draw_loop()
        finally:
            beat_task.cancel()
            key_task.cancel()

    async def _key_loop(self):
        while True:
            msg   = await self._keys.get()
            key   = msg.get('key')
            event = msg.get('event')
            if not (key and event == 'press'):
                continue
            if key == 'SELECT':
                if self._confirm:
                    self._confirm = False
                    self._enter()
                else:
                    self._advance()
            elif key == 'DOWN':
                self._advance()
            elif key in ('UP', 'BACK'):
                self._go_up()
            elif key == 'ENTER':
                self._confirm = False
                self._enter()

    async def _draw_loop(self):
        while True:
            self._draw()
            await asyncio.sleep_ms(_REFRESH_MS)

    # ── Draw ─────────────────────────────────────────────────────────────────

    def _draw_menubar(self, ops):
        ops.append({'cmd': 'fill_rect', 'x': 0, 'y': 0,
                    'w': self._w, 'h': _MENUBAR_H, 'color': _WIN_BG})
        ops.append({'cmd': 'hline', 'x': 0, 'y': _MENUBAR_H - 1,
                    'w': self._w, 'color': _BLACK})
        ty = (_MENUBAR_H - 8) // 2
        ops.append({'cmd': 'text', 's': 'PURR', 'x': 4, 'y': ty, 'color': _BLACK})
        label = 'Finder'
        ops.append({'cmd': 'text', 's': label,
                    'x': (self._w - len(label) * 8) // 2, 'y': ty, 'color': _BLACK})

    def _draw(self):
        if self._small_mode:
            self._draw_small()
            return
        ops = []
        ops.append({'cmd': 'fill', 'color': _BG})
        self._draw_menubar(ops)
        # Window sits below the menu bar
        x, y, w, h = 0, _MENUBAR_H, self._w, self._h - _MENUBAR_H
        cx, cy, cw, ch = self._draw_win(ops, 'Finder - ' + self._path, x, y, w, h)
        rows = max(1, (ch - 14) // _ROW_H)
        scroll, items = self._visible(rows)
        for i, name in enumerate(items):
            idx    = scroll + i
            iy     = cy + i * _ROW_H
            is_sel = idx == self._cursor
            if is_sel:
                ops.append({'cmd': 'fill_rect', 'x': cx, 'y': iy,
                            'w': cw, 'h': _ROW_H - 1, 'color': _BLUE})
                fg = _WIN_BG
            else:
                fg = _BLACK
            if name == '..':
                lbl = '[..] up'
            else:
                is_dir, sz = self._stats.get(name, (False, 0))
                lbl = ('[+] ' + name + '/') if is_dir else (name + '  ' + _fsize(sz))
            ops.append({'cmd': 'text', 's': lbl[:cw//8],
                        'x': cx + 2, 'y': iy + 2, 'color': fg})
        # Status bar at bottom of content area
        sy = cy + ch - 12
        st = (self._status or
              ('SEL:open  BACK:up' if self._confirm else 'SEL:next  BACK:up'))
        ops.append({'cmd': 'hline', 'x': cx, 'y': sy - 2, 'w': cw, 'color': _GRAY})
        ops.append({'cmd': 'text',  's': st[:cw//8], 'x': cx + 2, 'y': sy, 'color': _GRAY})
        ops.append({'cmd': 'show'})
        self._core.publish('display', {'type': 'raw', 'ops': ops})

    def _draw_win(self, ops, title, x, y, w, h):
        ops.append({'cmd': 'fill_rect', 'x': x, 'y': y, 'w': w, 'h': h, 'color': _WIN_BG})
        ops.append({'cmd': 'hline', 'x': x,     'y': y,     'w': w, 'color': _BLACK})
        ops.append({'cmd': 'vline', 'x': x,     'y': y,     'h': h, 'color': _BLACK})
        ops.append({'cmd': 'hline', 'x': x,     'y': y+h-1, 'w': w, 'color': _BLACK})
        ops.append({'cmd': 'vline', 'x': x+w-1, 'y': y,     'h': h, 'color': _BLACK})
        ops.append({'cmd': 'fill_rect', 'x': x+1, 'y': y+1,
                    'w': w-2, 'h': _WIN_TH, 'color': _WIN_TTL})
        ops.append({'cmd': 'hline', 'x': x+1, 'y': y+_WIN_TH, 'w': w-2, 'color': _BLACK})
        # Close box
        bx, by = x + 4, y + 3
        ops.append({'cmd': 'fill_rect', 'x': bx,   'y': by,   'w': 9, 'h': 9, 'color': _WIN_BG})
        ops.append({'cmd': 'hline',     'x': bx,   'y': by,   'w': 9, 'color': _BLACK})
        ops.append({'cmd': 'vline',     'x': bx,   'y': by,   'h': 9, 'color': _BLACK})
        ops.append({'cmd': 'hline',     'x': bx,   'y': by+8, 'w': 9, 'color': _BLACK})
        ops.append({'cmd': 'vline',     'x': bx+8, 'y': by,   'h': 9, 'color': _BLACK})
        max_ch = max(1, (w - 30) // 8)
        tstr   = title[:max_ch]
        tx     = x + max(18, (w - len(tstr) * 8) // 2)
        ops.append({'cmd': 'text', 's': tstr, 'x': tx, 'y': y + 3, 'color': _BLACK})
        return x + 2, y + _WIN_TH + 1, w - 4, h - _WIN_TH - 3

    def _draw_small(self):
        lines = []
        p = self._path
        lines.append('>' + (('..' + p[-12:]) if len(p) > 14 else p))
        _, items = self._visible(5)
        s = self._scroll
        for i, name in enumerate(items):
            is_sel = (s + i) == self._cursor
            if name == '..':
                lines.append(('>' if is_sel else ' ') + '[..] up')
            else:
                is_dir, sz = self._stats.get(name, (False, 0))
                if is_dir:
                    lines.append(('>' if is_sel else ' ') + name + '/')
                else:
                    lines.append(('>' if is_sel else ' ') + name + ' ' + _fsize(sz))
        while len(lines) < 7:
            lines.append('')
        lines.append('SEL:next BCK:up')
        self._core.publish('display', {'type': 'text', 'lines': lines[:8]})

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
