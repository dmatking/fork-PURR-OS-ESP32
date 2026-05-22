import sys
sys.path.insert(0, '/lib')

import json
import uasyncio as asyncio

_DEVICE_JSON      = '/system/kernel.app/device.json'
_SUPPORTED_DISPLAYS = ('ssd1306', 'ili9341', 'ili9342', 'ili9488')


def load_device():
    with open(_DEVICE_JSON) as f:
        return json.load(f)


def register_modules(core, cfg):
    display = cfg.get('display')
    res     = cfg.get('display_res', [0, 0])

    if display in _SUPPORTED_DISPLAYS:
        from modules.display import DisplayModule
        core.register('display', DisplayModule, critical=True)

    if display in _SUPPORTED_DISPLAYS and res[1] >= 200:
        from modules.explorer import ExplorerModule
        core.register('explorer', ExplorerModule)

    radios = cfg.get('radios', [])
    if 'wifi' in radios:
        from modules.wifi import WifiModule
        core.register('wifi', WifiModule)
    if 'lora' in radios:
        from modules.lora import LoraModule
        core.register('lora', LoraModule)

    if cfg.get('buttons'):
        from modules.input import InputModule
        core.register('input', InputModule, critical=True)

    if cfg.get('verbose'):
        from modules.shell import ShellModule
        core.register('shell', ShellModule)


async def boot():
    cfg     = load_device()
    verbose = cfg.get('verbose', False)

    if verbose:
        print("[KITT] device:", cfg.get('device'))
        print("[KITT] display:", cfg.get('display'))
        print("[KITT] radios:", cfg.get('radios'))

    sys.path.insert(0, '/system/kernel.app')
    from core import NanoCore

    core = NanoCore()
    register_modules(core, cfg)
    await core.run()


asyncio.run(boot())
