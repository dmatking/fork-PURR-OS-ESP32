#!/usr/bin/env python3
"""
PURR OS display emulator — GUI window.

Listens on localhost TCP for draw commands from run.py and renders them
on a tkinter canvas.

Usage:
  python3 emulator.py [--scale N] [--width W] [--height H] [--port P]
  python3 emulator.py --1to1        # 1 display-pixel = 1 screen pixel
"""

import tkinter as tk
import socket
import json
import threading
import argparse

PORT    = 8765
FG      = '#ffffff'
BG      = '#000000'
CHROME  = '#1e1e1e'
BORDER  = '#3a3a3a'

_KEY_MAP = {
    'Return':    'SELECT',
    'Escape':    'BACK',
    'Up':        'UP',
    'Down':      'DOWN',
    'Left':      'LEFT',
    'Right':     'RIGHT',
}


class PurrDisplay:
    def __init__(self, width, height, scale, port):
        self.w     = width
        self.h     = height
        self.sc    = scale
        self.port  = port
        self._conn      = None
        self._conn_lock = threading.Lock()

        # ── tkinter window ──────────────────────────────────────────────────
        self.root = tk.Tk()
        self.root.title('PURR OS  {}×{}  @{}x'.format(width, height, scale))
        self.root.resizable(False, False)
        self.root.configure(bg=CHROME)

        # Display canvas
        self.canvas = tk.Canvas(
            self.root,
            width=width * scale,
            height=height * scale,
            bg=BG,
            highlightthickness=2,
            highlightbackground=BORDER,
        )
        self.canvas.pack(padx=10, pady=(10, 4))

        # Status bar
        self._status = tk.StringVar(value='waiting for kernel…')
        tk.Label(
            self.root,
            textvariable=self._status,
            font=('Menlo', 9),
            fg='#666',
            bg=CHROME,
        ).pack(pady=(0, 8))

        # Font for text() calls — Menlo monospace, sized to fit char cells
        fs = max(7, int(8 * scale * 0.78))
        self._font = ('Menlo', fs)

        # ── Keyboard bindings ────────────────────────────────────────────────
        self._bind_keys()
        self.canvas.focus_set()

        # ── TCP server ───────────────────────────────────────────────────────
        self._running = True
        threading.Thread(target=self._serve, daemon=True).start()

        self.root.protocol('WM_DELETE_WINDOW', self._quit)

    def _bind_keys(self):
        for tk_key, keycode in _KEY_MAP.items():
            self.canvas.bind('<{}>'.format(tk_key),
                            lambda e, k=keycode: self._on_key(k))

    def _on_key(self, keycode):
        with self._conn_lock:
            conn = self._conn
        if not conn:
            return
        try:
            msg = json.dumps({'cmd': 'key', 'key': keycode}) + '\n'
            conn.sendall(msg.encode())
            self.root.after(0, self._set_status, '→ {}'.format(keycode), '#0f0')
            self.root.after(200, self._set_status, '● kernel connected', '#4ec94e')
        except Exception:
            with self._conn_lock:
                self._conn = None

    # ── Server ───────────────────────────────────────────────────────────────

    def _serve(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        srv.bind(('127.0.0.1', self.port))
        srv.listen(1)
        srv.settimeout(1.0)
        print('[emulator] ready on 127.0.0.1:{}'.format(self.port))

        while self._running:
            try:
                conn, _ = srv.accept()
            except socket.timeout:
                continue

            self.root.after(0, self._set_status, '● kernel connected', '#4ec94e')
            with self._conn_lock:
                self._conn = conn
            buf = ''
            while self._running:
                try:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    buf += chunk.decode('utf-8', errors='replace')
                    while '\n' in buf:
                        line, buf = buf.split('\n', 1)
                        line = line.strip()
                        if line:
                            try:
                                self.root.after(0, self._dispatch, json.loads(line))
                            except json.JSONDecodeError:
                                pass
                except Exception:
                    break
            with self._conn_lock:
                self._conn = None
            conn.close()
            self.root.after(0, self._set_status, '○ kernel disconnected', '#888')

        srv.close()

    # ── Color helper ──────────────────────────────────────────────────────────

    def _col(self, c, default=0):
        """Convert a display color value to a tkinter hex color string.
        0 = black, 1 = white, >1 = raw RGB565."""
        c = c if c is not None else default
        if not c:   return BG
        if c == 1:  return FG
        r = ((c >> 11) & 0x1F) * 255 // 31
        g = ((c >> 5)  & 0x3F) * 255 // 63
        b = (c & 0x1F)         * 255 // 31
        return '#{:02x}{:02x}{:02x}'.format(r, g, b)

    # ── Draw dispatch ─────────────────────────────────────────────────────────

    def _dispatch(self, cmd):
        op = cmd.get('cmd')
        sc = self.sc

        if op == 'fill':
            col = self._col(cmd.get('color'), default=0)
            self.canvas.configure(bg=col)
            self.canvas.delete('all')

        elif op == 'fill_rect':
            x1 = cmd['x'] * sc;        y1 = cmd['y'] * sc
            x2 = x1 + cmd['w'] * sc;   y2 = y1 + cmd['h'] * sc
            col = self._col(cmd.get('color'), default=0)
            self.canvas.create_rectangle(x1, y1, x2, y2, fill=col, outline='')

        elif op == 'hline':
            x1 = cmd['x'] * sc;  y1 = cmd['y'] * sc
            x2 = x1 + cmd['w'] * sc
            col = self._col(cmd.get('color'), default=0)
            # Draw as a solid bar sc pixels tall for visibility at any scale
            self.canvas.create_rectangle(x1, y1, x2, y1 + max(1, sc // 2),
                                         fill=col, outline='')

        elif op == 'vline':
            x1 = cmd['x'] * sc;  y1 = cmd['y'] * sc
            y2 = y1 + cmd['h'] * sc
            col = self._col(cmd.get('color'), default=0)
            self.canvas.create_rectangle(x1, y1, x1 + max(1, sc // 2), y2,
                                         fill=col, outline='')

        elif op == 'text':
            tx  = cmd['x'] * sc
            ty  = cmd['y'] * sc
            col = self._col(cmd.get('color'), default=1)
            self.canvas.create_text(tx, ty, text=cmd.get('s', ''),
                                    font=self._font, fill=col, anchor='nw')

        elif op == 'show':
            self._set_status('● show()', '#4ec94e')

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _set_status(self, msg, color='#666'):
        self._status.set(msg)
        # find the status label and update its fg
        for w in self.root.winfo_children():
            if isinstance(w, tk.Label):
                w.configure(fg=color)

    def _quit(self):
        self._running = False
        self.root.destroy()

    def run(self):
        self.root.mainloop()


def main():
    p = argparse.ArgumentParser(description='PURR OS display emulator')
    p.add_argument('--scale',  type=int, default=2,
                   help='Pixels per display pixel (default 2). Use 1 for 1:1.')
    p.add_argument('--1to1',  dest='one_to_one', action='store_true',
                   help='1:1 mode — 1 display pixel = 1 screen pixel (same as --scale 1)')
    p.add_argument('--width',  type=int, default=320)
    p.add_argument('--height', type=int, default=240)
    p.add_argument('--port',   type=int, default=PORT)
    args = p.parse_args()

    scale = 1 if args.one_to_one else args.scale
    PurrDisplay(args.width, args.height, scale, args.port).run()


if __name__ == '__main__':
    main()
