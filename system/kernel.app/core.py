import uasyncio as asyncio
from aqueue import Queue

# Heartbeat timeout in ms — module must ping within this window or gets restarted
_HEARTBEAT_TIMEOUT = 5000
# Delay between restart attempts for a crashed module
_RESTART_DELAY = 1000


class ModuleRecord:
    def __init__(self, name, factory, critical):
        self.name = name
        self.factory = factory   # callable() -> module instance with .run() coroutine
        self.critical = critical
        self.instance = None
        self.last_beat = 0
        self.restarts = 0
        self.task = None


class NanoCore:
    """
    Minimal supervisor. Owns nothing but the module registry and IPC bus.
    If a module crashes, only that module restarts — everything else keeps running.
    Critical modules (display, input) that fail MAX_RESTARTS times trigger a full reboot.
    """

    MAX_RESTARTS = 5

    def __init__(self):
        self._modules = {}   # name -> ModuleRecord
        self._bus = {}       # channel -> list of asyncio.Queue
        self._running = False

    # ------------------------------------------------------------------
    # Module registry
    # ------------------------------------------------------------------

    def register(self, name, factory, critical=False):
        self._modules[name] = ModuleRecord(name, factory, critical)

    # ------------------------------------------------------------------
    # IPC bus
    # ------------------------------------------------------------------

    def subscribe(self, channel):
        q = Queue()
        self._bus.setdefault(channel, []).append(q)
        return q

    def publish(self, channel, msg):
        for q in self._bus.get(channel, []):
            try:
                q.put_nowait(msg)
            except Exception:
                pass  # full queue — drop, never block the publisher

    # ------------------------------------------------------------------
    # Heartbeat
    # ------------------------------------------------------------------

    def beat(self, name):
        import utime
        rec = self._modules.get(name)
        if rec:
            rec.last_beat = utime.ticks_ms()

    # ------------------------------------------------------------------
    # Core event loop
    # ------------------------------------------------------------------

    async def run(self):
        self._running = True
        print("[core] starting")

        for name, rec in self._modules.items():
            rec.task = asyncio.create_task(self._supervise(rec))

        await self._heartbeat_monitor()

    async def _supervise(self, rec):
        import utime
        while self._running:
            try:
                print("[core] starting module:", rec.name)
                rec.instance = rec.factory(self)
                rec.last_beat = utime.ticks_ms()
                await rec.instance.run()
                # run() returned cleanly — treat as intentional stop
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
            now = utime.ticks_ms()
            for name, rec in self._modules.items():
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

    def _trigger_reboot(self):
        print("[core] rebooting")
        import machine
        machine.reset()
