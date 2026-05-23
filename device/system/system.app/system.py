"""
system.app — app lifecycle manager.

Apps must expose a Module class with NAME attribute and async run() method.
They are launched as supervised NanoCore modules — same crash isolation and
heartbeat monitoring as kernel modules.

IPC channels:
  - system.app.launch:    subscribe — {app: name}
  - system.app.stop:      subscribe — {app: name}
  - system.app.launching: publish   — {name, module}
  - system.app.stopped:   publish   — {name}
  - system.app.error:     publish   — {name, error}
  - system.status:        publish   — {mem_free} every GC cycle
"""

import uasyncio as asyncio
import gc
import os

_BEAT_INTERVAL  = 2000
_GC_INTERVAL_MS = 30_000


class SystemModule:
    NAME = 'system'

    def __init__(self, core):
        self._core         = core
        self._running_apps = {}   # app name → module NAME

    async def run(self):
        launch_q = self._core.subscribe('system.app.launch')
        stop_q   = self._core.subscribe('system.app.stop')

        beat_task   = asyncio.create_task(self._heartbeat())
        launch_task = asyncio.create_task(self._launch_loop(launch_q))
        stop_task   = asyncio.create_task(self._stop_loop(stop_q))
        gc_task     = asyncio.create_task(self._gc_loop())

        try:
            while True:
                await asyncio.sleep_ms(5000)
        finally:
            beat_task.cancel()
            launch_task.cancel()
            stop_task.cancel()
            gc_task.cancel()

    # ── Launch ─────────────────────────────────────────────────────────────────

    async def _launch_loop(self, q):
        while True:
            msg  = await q.get()
            name = msg.get('app', '')
            if name:
                await self._launch_app(name)

    async def _launch_app(self, name):
        if name in self._running_apps:
            print('[system] already running:', name)
            return

        entry = self._find_app(name)
        if entry is None:
            print('[system] not found:', name)
            self._core.publish('system.app.error', {'name': name, 'error': 'not found'})
            return

        print('[system] loading:', name)
        try:
            with open(entry) as f:
                code = f.read()
            ctx = {}
            exec(code, ctx)

            Module = ctx.get('Module')
            if Module is None:
                print('[system] no Module class in:', name)
                self._core.publish('system.app.error', {'name': name, 'error': 'no Module class'})
                return

            module_name = getattr(Module, 'NAME', name)
            self._core.launch(module_name, Module)
            self._running_apps[name] = module_name
            self._core.publish('system.app.launching', {'name': name, 'module': module_name})
            print('[system] launched:', name, 'as module:', module_name)

        except Exception as e:
            print('[system] launch error:', name, e)
            self._core.publish('system.app.error', {'name': name, 'error': str(e)})

    # ── Stop ───────────────────────────────────────────────────────────────────

    async def _stop_loop(self, q):
        while True:
            msg  = await q.get()
            name = msg.get('app', '')
            if not name:
                continue
            if name in self._running_apps:
                module_name = self._running_apps.pop(name)
                self._core.stop(module_name)
                self._core.publish('system.app.stopped', {'name': name})
            else:
                print('[system] stop: not running:', name)

    # ── GC ─────────────────────────────────────────────────────────────────────

    async def _gc_loop(self):
        while True:
            await asyncio.sleep_ms(_GC_INTERVAL_MS)
            gc.collect()
            free = gc.mem_free()
            self._core.publish('system.status', {'mem_free': free})
            print('[system] gc: mem_free={}'.format(free))

    # ── Helpers ────────────────────────────────────────────────────────────────

    def _find_app(self, name):
        for path in (
            '/apps/{}.app/main.py'.format(name),
            '/apps/{}/main.py'.format(name),
        ):
            try:
                os.stat(path)
                return path
            except OSError:
                pass
        return None

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)


def register(core, cfg):
    core.register('system', SystemModule)
