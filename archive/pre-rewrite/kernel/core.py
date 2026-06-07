import uasyncio as asyncio
from aqueue import Queue

_HEARTBEAT_TIMEOUT = 5000
_RESTART_DELAY     = 1000


class ModuleRecord:
    def __init__(self, name, factory, critical):
        self.name     = name
        self.factory  = factory
        self.critical = critical
        self.instance = None
        self.last_beat = 0
        self.restarts  = 0
        self.task      = None


class NanoCore:
    """
    Minimal supervisor. Owns the module registry and IPC bus.
    Modules that crash are restarted automatically. Critical modules that exceed
    MAX_RESTARTS trigger a full reboot. Apps launched at runtime via launch() get
    the same supervision as kernel modules.
    """

    MAX_RESTARTS = 5

    def __init__(self, wdt=None):
        self._modules = {}
        self._bus     = {}
        self._running = False
        self._wdt     = wdt

    # ── Module registry ────────────────────────────────────────────────────────

    def register(self, name, factory, critical=False):
        """Register a module before run() is called."""
        self._modules[name] = ModuleRecord(name, factory, critical)

    def launch(self, name, factory, critical=False):
        """Register and immediately supervise a module, even after run() started."""
        if name in self._modules:
            print("[core] launch: already registered:", name)
            return
        rec = ModuleRecord(name, factory, critical)
        self._modules[name] = rec
        if self._running:
            rec.task = asyncio.create_task(self._supervise(rec))
            print("[core] launched module:", name)

    def restart(self, name):
        """Force-restart a module. Resets its crash counter."""
        rec = self._modules.get(name)
        if not rec:
            print("[core] restart: unknown module:", name)
            return
        if rec.task:
            rec.task.cancel()
        rec.instance = None
        rec.restarts = 0
        if self._running:
            rec.task = asyncio.create_task(self._supervise(rec))
        print("[core] restarted module:", name)

    def stop(self, name):
        """Stop a module and remove it from supervision entirely."""
        rec = self._modules.pop(name, None)
        if not rec:
            print("[core] stop: unknown module:", name)
            return
        if rec.task:
            rec.task.cancel()
        print("[core] stopped module:", name)

    def reboot(self):
        """Trigger a clean kernel reboot via the watchdog."""
        print("[core] reboot requested")
        self._trigger_reboot()

    # ── IPC bus ────────────────────────────────────────────────────────────────

    def subscribe(self, channel):
        q = Queue()
        self._bus.setdefault(channel, []).append(q)
        return q

    def publish(self, channel, msg):
        for q in self._bus.get(channel, []):
            try:
                q.put_nowait(msg)
            except Exception:
                pass

    # ── Heartbeat ──────────────────────────────────────────────────────────────

    def beat(self, name):
        import utime
        rec = self._modules.get(name)
        if rec:
            rec.last_beat = utime.ticks_ms()

    # ── Core event loop ────────────────────────────────────────────────────────

    async def run(self):
        self._running = True
        print("[core] starting")

        for name, rec in self._modules.items():
            rec.task = asyncio.create_task(self._supervise(rec))

        # IPC-driven control channels
        restart_q = self.subscribe('core.restart')
        reboot_q  = self.subscribe('core.reboot')
        asyncio.create_task(self._restart_loop(restart_q))
        asyncio.create_task(self._reboot_loop(reboot_q))

        await self._heartbeat_monitor()

    async def _supervise(self, rec):
        import utime
        while self._running:
            try:
                print("[core] starting module:", rec.name)
                rec.instance = rec.factory(self)
                rec.last_beat = utime.ticks_ms()
                await rec.instance.run()
                print("[core] module stopped cleanly:", rec.name)
                break
            except Exception as e:
                rec.restarts += 1
                print("[core] module crashed:", rec.name, e, "restarts:", rec.restarts)
                self.publish("core.crash", {"module": rec.name, "error": str(e), "restarts": rec.restarts})

                if rec.critical and rec.restarts >= self.MAX_RESTARTS:
                    print("[core] critical module exceeded restart limit:", rec.name)
                    self.publish("core.fatal", {"module": rec.name})
                    self._trigger_reboot()
                    return

                await asyncio.sleep_ms(_RESTART_DELAY)

    async def _heartbeat_monitor(self):
        import utime
        while self._running:
            await asyncio.sleep_ms(2000)
            if self._wdt:
                self._wdt.feed()
            now = utime.ticks_ms()
            for name, rec in list(self._modules.items()):
                if rec.instance is None:
                    continue
                age = utime.ticks_diff(now, rec.last_beat)
                if age > _HEARTBEAT_TIMEOUT:
                    print("[core] heartbeat timeout:", name, "age:", age)
                    self.publish("core.timeout", {"module": name})
                    if rec.task:
                        rec.task.cancel()
                    rec.instance = None
                    rec.task = asyncio.create_task(self._supervise(rec))

    # ── Control channel listeners ──────────────────────────────────────────────

    async def _restart_loop(self, q):
        while self._running:
            msg  = await q.get()
            name = msg.get('module')
            if name:
                self.restart(name)

    async def _reboot_loop(self, q):
        while self._running:
            await q.get()
            self.reboot()

    def _trigger_reboot(self):
        print("[core] rebooting")
        import machine
        machine.reset()
