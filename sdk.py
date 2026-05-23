#!/usr/bin/env python3
"""
PURR OS SDK — developer tool for managing apps and kernel modules.

Usage:
    python3 sdk.py [device_root]

device_root defaults to ./device if it exists, else the current directory.
"""

import os
import sys
import shutil
import json

# ── ANSI colours ──────────────────────────────────────────────────────────────

_R  = '\033[0m'
_B  = '\033[1m'
_DIM = '\033[2m'
_CYAN  = '\033[96m'
_GREEN = '\033[92m'
_YELLOW = '\033[93m'
_RED   = '\033[91m'
_BLUE  = '\033[94m'

def _h(s):   return _B + _CYAN + s + _R
def _ok(s):  return _GREEN + s + _R
def _warn(s): return _YELLOW + s + _R
def _err(s): return _RED + s + _R
def _dim(s): return _DIM + s + _R

# ── App template ──────────────────────────────────────────────────────────────

_APP_TEMPLATE = '''\
"""
{name} — PURR OS app.

Drop this bundle into /apps/{name}.app/ on the device.
Launch via: core.publish('system.app.launch', {{'app': '{name}'}})

IPC conventions:
  - Subscribe to channels with core.subscribe('channel.name')
  - Publish with core.publish('channel.name', dict)
  - Call core.beat(self.NAME) regularly (at least every 5s) to stay alive
"""

import uasyncio as asyncio

_BEAT_INTERVAL = 2000


class Module:
    NAME = '{name}'

    def __init__(self, core):
        self._core = core

    async def run(self):
        print('[{name}] started')
        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._main_loop()
        finally:
            beat_task.cancel()

    async def _main_loop(self):
        # TODO: subscribe to IPC channels and do your thing
        while True:
            await asyncio.sleep_ms(1000)

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
'''

# ── SDK class ─────────────────────────────────────────────────────────────────

class SDK:
    def __init__(self, device_root):
        self.root = os.path.abspath(device_root)
        self.apps_dir    = os.path.join(self.root, 'apps')
        self.system_dir  = os.path.join(self.root, 'system')
        self.kernel_dir  = os.path.join(self.system_dir, 'kernel.app')
        self.modules_dir = os.path.join(self.kernel_dir, 'modules')
        self.device_json = os.path.join(self.kernel_dir, 'device.json')

    # ── Validation ─────────────────────────────────────────────────────────────

    def check(self):
        if not os.path.isdir(self.root):
            print(_err('Device root not found: ' + self.root))
            sys.exit(1)
        if not os.path.isfile(self.device_json):
            print(_err('device.json not found — is this a PURR OS device root?'))
            sys.exit(1)

    # ── Device config ──────────────────────────────────────────────────────────

    def load_cfg(self):
        with open(self.device_json) as f:
            return json.load(f)

    def save_cfg(self, cfg):
        with open(self.device_json, 'w') as f:
            json.dump(cfg, f, indent=2)
        print(_ok('  device.json saved.'))

    # ── App listing ────────────────────────────────────────────────────────────

    def list_apps(self):
        os.makedirs(self.apps_dir, exist_ok=True)
        apps = []
        for entry in sorted(os.listdir(self.apps_dir)):
            path = os.path.join(self.apps_dir, entry)
            if entry.endswith('.app') and os.path.isdir(path):
                has_main = os.path.isfile(os.path.join(path, 'main.py'))
                apps.append((entry[:-4], has_main))
        return apps

    def list_system_apps(self):
        apps = []
        for entry in sorted(os.listdir(self.system_dir)):
            path = os.path.join(self.system_dir, entry)
            if entry.endswith('.app') and os.path.isdir(path):
                apps.append(entry[:-4])
        return apps

    def list_kernel_modules(self):
        mods = []
        if os.path.isdir(self.modules_dir):
            for f in sorted(os.listdir(self.modules_dir)):
                if f.endswith('.py') and not f.startswith('_'):
                    mods.append(f[:-3])
        return mods

    # ── App management ─────────────────────────────────────────────────────────

    def new_app(self, name):
        dest = os.path.join(self.apps_dir, name + '.app')
        if os.path.exists(dest):
            print(_warn('  App already exists: ' + dest))
            return False
        os.makedirs(dest, exist_ok=True)
        main_path = os.path.join(dest, 'main.py')
        with open(main_path, 'w') as f:
            f.write(_APP_TEMPLATE.format(name=name))
        print(_ok('  Created: ' + main_path))
        print(_dim('  Edit main.py, then launch with:'))
        print(_dim("  core.publish('system.app.launch', {'app': '" + name + "'})"))
        return True

    def install_app(self, src_path):
        src_path = os.path.abspath(src_path)
        if not os.path.isdir(src_path):
            print(_err('  Source path not found: ' + src_path))
            return False
        name = os.path.basename(src_path)
        if not name.endswith('.app'):
            name += '.app'
        dest = os.path.join(self.apps_dir, name)
        if os.path.exists(dest):
            shutil.rmtree(dest)
        shutil.copytree(src_path, dest)
        print(_ok('  Installed: ' + dest))
        return True

    def remove_app(self, name):
        dest = os.path.join(self.apps_dir, name + '.app')
        if not os.path.isdir(dest):
            dest = os.path.join(self.apps_dir, name)
        if not os.path.exists(dest):
            print(_err('  App not found: ' + name))
            return False
        shutil.rmtree(dest)
        print(_ok('  Removed: ' + dest))
        return True

    # ── Kernel module toggle ────────────────────────────────────────────────────

    def toggle_radio(self, radio, enable):
        cfg = self.load_cfg()
        radios = cfg.get('radios', [])
        if enable and radio not in radios:
            radios.append(radio)
            cfg['radios'] = radios
            self.save_cfg(cfg)
            print(_ok('  Enabled radio: ' + radio))
        elif not enable and radio in radios:
            radios.remove(radio)
            cfg['radios'] = radios
            self.save_cfg(cfg)
            print(_ok('  Disabled radio: ' + radio))
        else:
            state = 'already enabled' if enable else 'already disabled'
            print(_dim('  ' + radio + ' ' + state))


# ── Menu helpers ──────────────────────────────────────────────────────────────

def _prompt(prompt, default=''):
    try:
        val = input(prompt).strip()
        return val if val else default
    except (KeyboardInterrupt, EOFError):
        return None

def _banner(sdk):
    cfg = sdk.load_cfg()
    print()
    print(_h('╔═══════════════════════════════════════╗'))
    print(_h('║') + _B + '        PURR OS  SDK                   ' + _h('║'))
    print(_h('╚═══════════════════════════════════════╝'))
    print(_dim('  Device: ') + cfg.get('device', '?') +
          _dim('   Display: ') + cfg.get('display', '?') +
          _dim('   Radios: ') + ', '.join(cfg.get('radios', [])))
    print(_dim('  Root: ') + sdk.root)
    print()

def _section(title):
    print()
    print(_BLUE + _B + '  ' + title + _R)
    print(_dim('  ' + '─' * 40))

# ── Screens ───────────────────────────────────────────────────────────────────

def screen_main(sdk):
    while True:
        _banner(sdk)
        _section('Apps  (/apps/)')
        print('    [a]  List installed apps')
        print('    [n]  New app  (scaffold)')
        print('    [i]  Install app  (from path)')
        print('    [r]  Remove app')
        _section('Kernel')
        print('    [k]  List kernel modules')
        print('    [s]  List system apps  (/system/*.app)')
        print('    [c]  Edit device config')
        _section('Info')
        print('    [?]  IPC channel reference')
        print('    [q]  Quit')
        print()
        choice = _prompt('  > ')
        if choice is None or choice == 'q':
            print()
            break
        elif choice == 'a':
            screen_list_apps(sdk)
        elif choice == 'n':
            screen_new_app(sdk)
        elif choice == 'i':
            screen_install_app(sdk)
        elif choice == 'r':
            screen_remove_app(sdk)
        elif choice == 'k':
            screen_kernel_modules(sdk)
        elif choice == 's':
            screen_system_apps(sdk)
        elif choice == 'c':
            screen_device_config(sdk)
        elif choice == '?':
            screen_ipc_ref()
        else:
            print(_warn('  Unknown option.'))


def screen_list_apps(sdk):
    _section('Installed Apps')
    apps = sdk.list_apps()
    if not apps:
        print(_dim('    (none — use [n] to scaffold or [i] to install)'))
    for name, ok in apps:
        status = _ok('✓') if ok else _warn('✗ no main.py')
        print('    {} {}'.format(status, name))
    _prompt('\n  Press Enter to continue ')


def screen_new_app(sdk):
    _section('New App')
    name = _prompt('  App name (letters/digits/underscores): ')
    if not name:
        return
    name = name.strip().replace(' ', '_')
    if not name.replace('_', '').isalnum():
        print(_err('  Invalid name.'))
        return
    sdk.new_app(name)
    _prompt('\n  Press Enter to continue ')


def screen_install_app(sdk):
    _section('Install App')
    path = _prompt('  Path to .app directory: ')
    if not path:
        return
    sdk.install_app(path)
    _prompt('\n  Press Enter to continue ')


def screen_remove_app(sdk):
    _section('Remove App')
    apps = sdk.list_apps()
    if not apps:
        print(_dim('    (no apps installed)'))
        _prompt('\n  Press Enter to continue ')
        return
    for i, (name, _) in enumerate(apps):
        print('    [{}]  {}'.format(i + 1, name))
    choice = _prompt('\n  Enter number (or Enter to cancel): ')
    if not choice:
        return
    try:
        idx = int(choice) - 1
        name = apps[idx][0]
        confirm = _prompt('  Remove "{}"? [y/N]: '.format(name))
        if confirm and confirm.lower() == 'y':
            sdk.remove_app(name)
    except (ValueError, IndexError):
        print(_err('  Invalid selection.'))
    _prompt('\n  Press Enter to continue ')


def screen_kernel_modules(sdk):
    _section('Kernel Modules  (system/kernel.app/modules/)')
    mods = sdk.list_kernel_modules()
    cfg  = sdk.load_cfg()
    radios   = cfg.get('radios', [])
    buttons  = cfg.get('buttons', {})
    verbose  = cfg.get('verbose', False)
    display  = cfg.get('display', '')

    _SUPPORTED = ('ssd1306', 'ili9341', 'ili9342', 'ili9488')

    for m in mods:
        # Determine active state based on device.json
        if m == 'display':
            active = display in _SUPPORTED
        elif m == 'explorer' or m == 'explorer_lvgl' or m == 'lvgl_layout':
            active = display in _SUPPORTED
        elif m in ('wifi',):
            active = 'wifi' in radios
        elif m == 'lora':
            active = 'lora' in radios
        elif m == 'input':
            active = bool(buttons)
        elif m == 'shell':
            active = verbose
        else:
            active = True
        flag = _ok('active') if active else _dim('inactive')
        print('    {}  {}'.format(flag.ljust(20), m))

    print()
    print(_dim('  Radios: ') + ', '.join(radios) if radios else _dim('  No radios enabled'))
    print()

    toggle = _prompt('  Toggle a radio? [wifi/lora/bt/Enter to skip]: ')
    if toggle in ('wifi', 'lora', 'bt'):
        current = toggle in radios
        sdk.toggle_radio(toggle, not current)

    _prompt('\n  Press Enter to continue ')


def screen_system_apps(sdk):
    _section('System Apps  (system/*.app)')
    apps = sdk.list_system_apps()
    for name in apps:
        flag = _ok('●') if name != 'kernel' else _CYAN + '●' + _R
        print('    {}  {}'.format(flag, name))
    print()
    print(_dim('  System apps are auto-discovered by KITT. Place a .app dir in'))
    print(_dim('  device/system/ with a register(core, cfg) function at the root.'))
    _prompt('\n  Press Enter to continue ')


def screen_device_config(sdk):
    _section('Device Config  (device.json)')
    cfg = sdk.load_cfg()
    print(json.dumps(cfg, indent=2))
    print()
    print(_dim('  Keys you can edit:'))
    print(_dim('  device, display, display_res, radios, buttons, keymap, verbose'))
    print()
    key = _prompt('  Key to edit (or Enter to cancel): ')
    if not key:
        return
    current = cfg.get(key, '<not set>')
    print(_dim('  Current: ') + repr(current))
    val_str = _prompt('  New value (JSON): ')
    if val_str is None:
        return
    try:
        cfg[key] = json.loads(val_str)
        sdk.save_cfg(cfg)
    except json.JSONDecodeError as e:
        print(_err('  Invalid JSON: ' + str(e)))
    _prompt('\n  Press Enter to continue ')


def screen_ipc_ref():
    _section('IPC Channel Reference')
    channels = [
        ('input.raw',           'input',     'bridge',   '{button, event}'),
        ('input.key',           'bridge',    'explorer', '{key, event}'),
        ('bridge.handoff',      'explorer',  'bridge',   '{target}'),
        ('bridge.handoff.ack',  'bridge',    'any',      '{target, status}'),
        ('bridge.suspend',      'bridge',    'explorer', '{target}'),
        ('bridge.resume',       'bridge',    'explorer', '{target}'),
        ('explorer.tray',       'wifi/lora', 'explorer', '{wifi: bool, lora: bool}'),
        ('system.app.launch',   'explorer',  'system',   "{app: 'name'}"),
        ('system.app.stop',     'any',       'system',   "{app: 'name'}"),
        ('system.app.launching','system',    'any',      '{name, module}'),
        ('system.app.exited',   'system',    'any',      '{name}'),
        ('system.app.error',    'system',    'any',      '{name, error}'),
        ('system.status',       'system',    'any',      '{mem_free}'),
        ('lora.rx',             'lora',      'any',      '{payload, rssi, snr}'),
        ('lora.tx',             'any',       'lora',     "{type:'send', payload}"),
        ('lora.status',         'lora',      'any',      '{ready, spoof}'),
        ('wifi.status',         'wifi',      'any',      '{connected, ifconfig}'),
        ('core.restart',        'any',       'core',     "{module: 'name'}"),
        ('core.reboot',         'any',       'core',     '{}'),
        ('core.crash',          'core',      'any',      '{module, error, restarts}'),
        ('core.fatal',          'core',      'any',      '{module}'),
    ]
    col = max(len(c[0]) for c in channels) + 2
    print('    ' + _B + 'Channel'.ljust(col) + 'Publisher'.ljust(14) + 'Subscriber'.ljust(14) + 'Payload' + _R)
    print('    ' + _dim('─' * (col + 14 + 14 + 30)))
    for ch, pub, sub, payload in channels:
        print('    ' + _CYAN + ch.ljust(col) + _R +
              _dim(pub.ljust(14)) +
              _dim(sub.ljust(14)) +
              payload)
    _prompt('\n  Press Enter to continue ')


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    cwd = os.getcwd()
    if len(sys.argv) > 1:
        device_root = sys.argv[1]
    else:
        candidate = os.path.join(cwd, 'device')
        device_root = candidate if os.path.isdir(candidate) else cwd

    sdk = SDK(device_root)
    sdk.check()

    try:
        screen_main(sdk)
    except (KeyboardInterrupt, EOFError):
        print()


if __name__ == '__main__':
    main()
