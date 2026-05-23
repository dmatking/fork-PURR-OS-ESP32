import sys
sys.path.insert(0, '/lib')

import os
import json
import uasyncio as asyncio

_DEVICE_JSON        = '/system/kernel.app/device.json'
_SUPPORTED_DISPLAYS = ('ssd1306', 'ili9341', 'ili9342', 'ili9488')


def load_device():
    with open(_DEVICE_JSON) as f:
        return json.load(f)


def register_modules(core, cfg):
    """Register KITT's own hardware modules (display, input, radios, shell)."""
    display = cfg.get('display')
    res     = cfg.get('display_res', [0, 0])

    if display in _SUPPORTED_DISPLAYS:
        from modules.display import DisplayModule
        core.register('display', DisplayModule, critical=True)

    if display in _SUPPORTED_DISPLAYS:
        from modules.explorer import ExplorerModule
        core.register('explorer', ExplorerModule)

    radios = cfg.get('radios', [])
    if 'wifi' in radios:
        from modules.wifi import WifiModule
        core.register('wifi', WifiModule)
    if 'lora' in radios:
        from modules.lora import LoraModule
        core.register('lora', LoraModule)
        try:
            from modules.remoteass import RemoteShellModule
            core.register('remoteshell', RemoteShellModule)
        except ImportError:
            pass
        try:
            from modules.netboot import NetBootModule
            core.register('netboot', NetBootModule)
        except ImportError:
            pass

    if cfg.get('buttons'):
        from modules.input import InputModule
        core.register('input', InputModule, critical=True)

    if cfg.get('verbose'):
        from modules.shell import ShellModule
        core.register('shell', ShellModule)


def register_apps(core, cfg):
    """Auto-discover system apps. Each app exposes register(core, cfg) in its root module."""
    try:
        entries = sorted(os.listdir('/system'))
    except OSError:
        return

    for entry in entries:
        if not entry.endswith('.app') or entry == 'kernel.app':
            continue
        appname = entry[:-4]   # strip '.app'
        try:
            mod = __import__(appname)
            if hasattr(mod, 'register'):
                mod.register(core, cfg)
                print('[KITT] registered app:', appname)
        except ImportError:
            pass   # app has no registrar — skip silently
        except Exception as e:
            print('[KITT] app register failed:', appname, e)


async def boot():
    cfg     = load_device()
    verbose = cfg.get('verbose', False)

    if verbose:
        print("[KITT] device:", cfg.get('device'))
        print("[KITT] display:", cfg.get('display'))
        print("[KITT] radios:", cfg.get('radios'))

    sys.path.insert(0, '/system/kernel.app')
    try:
        for entry in os.listdir('/system'):
            if entry.endswith('.app') and entry != 'kernel.app':
                sys.path.insert(0, '/system/' + entry)
    except OSError:
        pass

    from core import NanoCore
    core = NanoCore(wdt=globals().get('_wdt'))

    register_modules(core, cfg)
    register_apps(core, cfg)

    await core.run()


asyncio.run(boot())
