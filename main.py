import sys
sys.path.insert(0, '/lib')

import json
import machine

# --- load device profile ---
try:
    with open('/system/kernel.app/device.json') as f:
        _cfg = json.load(f)
except Exception:
    _cfg = {}

_device_name = _cfg.get('device', 'purr_os')

# --- splash (device-adaptive) ---
try:
    from display_factory import make_display
    _d = make_display(_cfg)
    if _d:
        _w, _h = _d.width, _d.height
        _sc = getattr(_d, 'scale', 1)
        _cw = 8 * _sc   # char width in pixels
        _ch = 8 * _sc   # char height in pixels

        _d.fill(0)
        _d.text("PURR  OS",        _w // 2 - 4 * _cw,                        _h // 8)
        _d.hline(0,                _h // 4,             _w, 1)
        _d.text("KITT  v0.1",      _w // 2 - 5 * _cw,                        _h * 3 // 8)
        _d.text(_device_name[:16], _w // 2 - len(_device_name[:16]) * _cw // 2, _h // 2)
        _d.hline(0,                _h * 3 // 4,         _w, 1)
        _d.text("Booting...",      _w // 2 - 5 * _cw,                        _h * 7 // 8)
        _d.show()
        print("[main] display OK")
    else:
        print("[main] no driver for:", _cfg.get('display'))
except Exception as _e:
    print("[main] display FAIL:", _e)

# --- hand off to watchdog ---
print("[main] starting watchdog")
exec(open('/boot/watchdog.py').read())
