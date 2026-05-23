"""
PURR OS — local dev runner (MicroPython Unix port).

Patches hardware modules so the kernel runs on your Mac with no emulator.
Run with:  micropython run.py [device_root] [--device NAME]

device_root defaults to ./device/ if it exists, else the current directory.
--device NAME loads system/kernel.app/devices/NAME.json as device.json.
The emulate script passes both automatically.
"""

import sys
import os
import builtins

# ── Capture originals before any patching ────────────────────────────────────
_real_stat    = os.stat
_real_listdir = os.listdir
_real_open    = builtins.open

def _exists(path):
    try:
        _real_stat(path)
        return True
    except OSError:
        return False

# ── Parse argv: [device_root] [--device NAME] ────────────────────────────────
_cwd         = os.getcwd()
_device_name = None
_positional  = []

_i = 1
while _i < len(sys.argv):
    if sys.argv[_i] == '--device' and _i + 1 < len(sys.argv):
        _device_name = sys.argv[_i + 1]
        _i += 2
    else:
        _positional.append(sys.argv[_i])
        _i += 1

# ── Device root ──────────────────────────────────────────────────────────────
# Kernel code uses absolute paths like /system/kernel.app/device.json.
# PROJECT_ROOT is the local directory that maps to the device's / (root).

if _positional:
    PROJECT_ROOT = _positional[0]
    if not PROJECT_ROOT.startswith('/'):
        PROJECT_ROOT = _cwd + '/' + PROJECT_ROOT
else:
    _device = _cwd + '/device'
    PROJECT_ROOT = _device if _exists(_device) else _cwd

# ── Device profile: redirect device.json → devices/NAME.json ─────────────────
_device_profile = None
if _device_name:
    _dp = PROJECT_ROOT + '/system/kernel.app/devices/' + _device_name + '.json'
    if _exists(_dp):
        _device_profile = _dp
        print('[run] device profile: {}'.format(_device_name))
    else:
        print('[run] device profile not found: {} (using device.json)'.format(_device_name))

# ── 0. Emulator shared state ─────────────────────────────────────────────────

class _EmuState:
    injected_keys = []

_emu_mod = _EmuState()
sys.modules['_purr_emulator'] = _emu_mod

# ── 1. Redirect absolute paths → PROJECT_ROOT ────────────────────────────────
# Kernel uses /system/..., /lib/..., /apps/..., /friends/... etc.
# All those paths live under PROJECT_ROOT on the dev machine.
#
# MicroPython doesn't allow attribute assignment on built-in modules, so we
# can't do `os.stat = patched_fn`. Instead we replace sys.modules['os'] with
# a proxy that delegates unknown attributes to the real module.

def _open(path, *args, **kwargs):
    if _device_profile and isinstance(path, str) and path == '/system/kernel.app/device.json':
        return _real_open(_device_profile, *args, **kwargs)
    if isinstance(path, str) and path.startswith('/') and not _exists(path):
        local = PROJECT_ROOT + path
        if _exists(local):
            return _real_open(local, *args, **kwargs)
    return _real_open(path, *args, **kwargs)

builtins.open = _open

_real_os = sys.modules.get('os') or sys.modules.get('uos') or os

class _OsPatch:
    def __getattr__(self, name):
        return getattr(_real_os, name)

    def stat(self, path):
        if isinstance(path, str) and path.startswith('/'):
            local = PROJECT_ROOT + path
            try:
                return _real_stat(local)
            except OSError:
                pass
        return _real_stat(path)

    def listdir(self, path='.'):
        if isinstance(path, str) and path.startswith('/'):
            local = PROJECT_ROOT + path
            if _exists(local):
                return _real_listdir(local)
        return _real_listdir(path)

_os_patch = _OsPatch()
sys.modules['os']  = _os_patch
sys.modules['uos'] = _os_patch

# ── 2. Pre-populate sys.path ─────────────────────────────────────────────────
# KITT adds /system/*.app and /lib to sys.path at runtime, but those absolute
# paths won't resolve on Mac. Add the local equivalents now so imports work
# before the kernel's own path setup runs.

for _app in _real_listdir(PROJECT_ROOT + '/system'):
    if _app.endswith('.app'):
        sys.path.insert(0, PROJECT_ROOT + '/system/' + _app)
sys.path.insert(0, PROJECT_ROOT + '/lib')
# Do NOT add PROJECT_ROOT itself — device/system/ would shadow system.app as a namespace package

# ── 3. Mock: utime ───────────────────────────────────────────────────────────
#    The real utime.sleep_ms would burn ~1s during hardware init delays.
#    We suppress those while keeping ticks_* intact for asyncio + timeouts.

import utime as _utime

class _MockUtime:
    ticks_ms   = staticmethod(_utime.ticks_ms)
    ticks_diff = staticmethod(_utime.ticks_diff)
    ticks_add  = staticmethod(_utime.ticks_add)
    ticks_us   = staticmethod(_utime.ticks_us)
    time       = staticmethod(_utime.time)

    @staticmethod
    def sleep_ms(ms):
        pass  # suppress hardware-init delays (ILI9341 reset, SLPOUT, etc.)

    @staticmethod
    def sleep(s):
        pass

sys.modules['utime'] = _MockUtime()

# ── 4. Mock: machine ─────────────────────────────────────────────────────────
#    Unix machine has no Pin / SPI / SoftI2C / WDT — add them all.

import machine as _machine_base

class _Pin:
    IN = OUT = PULL_UP = PULL_DOWN = OPEN_DRAIN = ALT = 0

    def __init__(self, id, mode=0, pull=0, value=None, **kw):
        self._id  = id
        self._val = 1 if value is None else int(value)

    def value(self, v=None):
        if v is None:
            return self._val
        self._val = int(v)

    def __call__(self, v=None):
        return self.value(v)

    def __repr__(self):
        return 'Pin({})'.format(self._id)


class _SoftI2C:
    def __init__(self, scl=None, sda=None, freq=400_000, **kw):
        pass
    def writeto(self, addr, buf, stop=True):
        pass
    def readfrom(self, addr, n):
        return bytes(n)
    def scan(self):
        return [0x3C]


class _SPI:
    def __init__(self, id=1, baudrate=1_000_000, **kw):
        pass
    def write(self, buf):
        pass
    def read(self, n, write=0):
        return bytes(n)
    def write_readinto(self, buf, out):
        pass


class _WDT:
    def __init__(self, id=0, timeout=5000):
        print('[mock] WDT armed ({}ms) — no-op on desktop'.format(timeout))
    def feed(self):
        pass


class _MockMachine:
    Pin      = _Pin
    SoftI2C  = _SoftI2C
    SPI      = _SPI
    WDT      = _WDT
    PinBase  = _machine_base.PinBase
    Signal   = _machine_base.Signal
    soft_reset = _machine_base.soft_reset

    @staticmethod
    def reset():
        print('[mock] machine.reset() called — stopping')
        sys.exit(0)

    @staticmethod
    def freq(f=None):
        return 240_000_000 if f is None else None

sys.modules['machine'] = _MockMachine()

# ── 5. Mock: network ─────────────────────────────────────────────────────────

class _WLAN:
    STA_IF = AP_IF = 0

    def __init__(self, iface=0):
        self._active    = False
        self._connected = False

    def active(self, v=None):
        if v is None:
            return self._active
        self._active = bool(v)

    def connect(self, ssid, pw=None):
        print('[mock] WLAN.connect({})'.format(ssid))

    def disconnect(self):
        self._connected = False

    def isconnected(self):
        return self._connected

    def scan(self):
        return []

    def ifconfig(self):
        return ('0.0.0.0', '255.255.255.0', '0.0.0.0', '0.0.0.0')

    def config(self, param):
        if param == 'mac':
            return b'\xde\xad\xbe\xef\x00\x01'
        return None

    def status(self, param=None):
        return 0


class _MockNetwork:
    WLAN    = _WLAN
    STA_IF  = _WLAN.STA_IF
    AP_IF   = _WLAN.AP_IF

sys.modules['network'] = _MockNetwork()

# ── 6. Terminal display emulator ─────────────────────────────────────────────
#    Intercepts all drawing calls and renders the display as a character grid
#    in the terminal.  show() clears the screen and redraws — just like a real
#    display refreshing.  Replaces display_factory.make_display() entirely so
#    no SPI/I2C hardware paths are exercised.

class _TerminalDisplay:
    def __init__(self, width=320, height=240, scale=2):
        self.width  = width
        self.height = height
        self.scale  = scale
        # Character size: always 8px, scale affects rendering only
        self._cw    = 8        # pixel width of one character
        self._ch    = 8        # pixel height of one character
        self._cols  = width  // self._cw
        self._rows  = height // self._ch
        self._grid  = [[' '] * self._cols for _ in range(self._rows)]
        self._hline_rows = set()  # char rows that contain a horizontal rule

    def fill(self, color):
        ch = '█' if color else ' '
        for r in range(self._rows):
            self._grid[r] = [ch] * self._cols
        if not color:
            self._hline_rows.clear()

    def fill_rect(self, x, y, w, h, color):
        ch   = '█' if color else ' '
        c0   = max(0,  x // self._cw)
        r0   = max(0,  y // self._ch)
        c1   = min(self._cols, (x + w - 1) // self._cw + 1)
        r1   = min(self._rows, (y + h - 1) // self._ch + 1)
        for r in range(r0, r1):
            for c in range(c0, c1):
                self._grid[r][c] = ch
        if not color:
            for r in range(r0, r1):
                self._hline_rows.discard(r)

    def hline(self, x, y, w, color):
        if color:
            self._hline_rows.add(y // self._ch)
        else:
            self._hline_rows.discard(y // self._ch)

    def vline(self, x, y, h, color):
        pass

    def text(self, s, x, y, color=1):
        col = x // self._cw
        row = y // self._ch
        if row < 0 or row >= self._rows:
            return
        for i, ch in enumerate(s):
            c = col + i
            if 0 <= c < self._cols:
                self._grid[row][c] = ch if color else ' '

    def show(self):
        out = ['\x1b[2J\x1b[H']   # clear screen, cursor home
        w = self._cols
        out.append('┌' + '─' * w + '┐')
        for r in range(self._rows):
            if r in self._hline_rows:
                out.append('├' + '─' * w + '┤')
            else:
                out.append('│' + ''.join(self._grid[r]) + '│')
        out.append('└' + '─' * w + '┘')
        sys.stdout.write('\n'.join(out) + '\n')
        sys.stdout.flush()


# ── 7. Socket display (GUI emulator) ─────────────────────────────────────────
#    Sends JSON draw commands to emulator.py over TCP.
#    Falls back to TerminalDisplay if the emulator window isn't running.

class _SocketDisplay:
    _PORT = 8765

    def __init__(self, width, height, scale):
        self.width  = width
        self.height = height
        self.scale  = scale
        self._sock  = None
        self._recv_buf = ''
        import socket, time
        addr = socket.getaddrinfo('127.0.0.1', self._PORT)[0][-1]
        for attempt in range(8):          # retry for up to ~1.6s
            try:
                s = socket.socket()
                s.connect(addr)
                self._sock = s
                print('[display] GUI emulator connected on port {}'.format(self._PORT))
                break
            except Exception:
                time.sleep(0.2)
        if self._sock:
            try:
                self._sock.setblocking(False)
            except Exception:
                pass

    def _poll_keys(self):
        """Poll for incoming key messages from emulator (non-blocking)."""
        if not self._sock:
            return
        try:
            chunk = self._sock.recv(256)
            if not chunk:
                self._sock = None
                return
            self._recv_buf += chunk.decode()
            while '\n' in self._recv_buf:
                line, self._recv_buf = self._recv_buf.split('\n', 1)
                line = line.strip()
                if not line:
                    continue
                # Parse: {"cmd": "key", "key": "UP"}
                if '"cmd": "key"' in line and '"key"' in line:
                    try:
                        key_start = line.find('"key": "') + 8
                        key_end = line.find('"', key_start)
                        if key_start > 7 and key_end > key_start:
                            keycode = line[key_start:key_end]
                            _emu_mod.injected_keys.append({'key': keycode})
                    except Exception:
                        pass
        except OSError:
            pass  # No data available (normal when non-blocking)
        except Exception:
            self._sock = None

    def _send(self, **kw):
        if not self._sock:
            return
        try:
            import json
            msg = json.dumps(kw) + '\n'
            self._sock.send(msg.encode())
        except Exception:
            self._sock = None

    def fill(self, color):              self._send(cmd='fill',      color=color)
    def fill_rect(self, x, y, w, h, c): self._send(cmd='fill_rect', x=x, y=y, w=w, h=h, color=c)
    def hline(self, x, y, w, color):   self._send(cmd='hline',     x=x, y=y, w=w, color=color)
    def vline(self, x, y, h, color):   self._send(cmd='vline',     x=x, y=y, h=h, color=color)
    def text(self, s, x, y, color=1):  self._send(cmd='text',      s=s, x=x, y=y, color=color)
    def show(self):
        self._poll_keys()  # Check for keys from emulator
        self._send(cmd='show')


# Patch display_factory — try GUI socket first, fall back to terminal.
import display_factory as _df

def _make_display(cfg):
    res   = cfg.get('display_res', [128, 64])
    scale = 2 if res[0] >= 200 else 1
    d = _SocketDisplay(res[0], res[1], scale)
    if d._sock is not None:
        return d
    print('[display] falling back to terminal renderer')
    return _TerminalDisplay(res[0], res[1], scale)

_df.make_display = _make_display

# ── 7. Launch kernel (bypass watchdog — run kernel directly) ─────────────────

print('=' * 44)
print('  PURR OS  —  local kernel  (MicroPython {})'.format(sys.version))
print('=' * 44)

try:
    kernel = PROJECT_ROOT + '/system/kernel.app/main.py'
    with _real_open(kernel) as f:
        exec(f.read(), {'__name__': '__main__'})
except KeyboardInterrupt:
    print('\n[run] stopped by user')
except SystemExit:
    pass
