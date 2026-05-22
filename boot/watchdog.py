import machine
import utime
import sys

# Hardware WDT timeout — must be fed within this window or chip resets
# 8s gives the kernel enough time to start up before the first feed
_HW_WDT_TIMEOUT = 8000

# How often watchdog feeds the HW WDT and checks kernel health
_POLL_INTERVAL = 2000

# If kernel misses this many polls, watchdog triggers a soft reset
_KERNEL_MISS_LIMIT = 3


class Watchdog:
    def __init__(self):
        self._wdt = machine.WDT(timeout=_HW_WDT_TIMEOUT)
        self._kernel_task = None
        self._misses = 0
        self._kernel_alive = False
        print("[watchdog] HW WDT armed, timeout:", _HW_WDT_TIMEOUT, "ms")

    def feed(self):
        self._wdt.feed()

    def run(self):
        self.feed()
        self._launch_kernel()

        while True:
            utime.sleep_ms(_POLL_INTERVAL)
            self.feed()
            self._check_kernel()

    def _launch_kernel(self):
        print("[watchdog] launching kernel")
        try:
            # exec avoids the circular-import problem that `import main` would cause
            # (it would re-import the root main.py, not the kernel entry point).
            with open('/system/kernel.app/main.py') as f:
                exec(f.read(), {'__name__': '__main__'})
            self._kernel_alive = True
        except Exception as e:
            print("[watchdog] kernel launch failed:", e)
            self._kernel_alive = False

    def _check_kernel(self):
        # Phase 0–1: kernel is in-process so if we get here it's still running.
        # Future: kernel will publish heartbeat events we can listen for.
        # For now, reaching this loop at all means the kernel asyncio loop is alive.
        self._misses = 0

        # Placeholder for out-of-process health check:
        # if not kernel_heartbeat_received():
        #     self._misses += 1
        #     if self._misses >= _KERNEL_MISS_LIMIT:
        #         print("[watchdog] kernel unresponsive, resetting")
        #         machine.reset()


def start():
    wd = Watchdog()
    try:
        wd.run()
    except Exception as e:
        print("[watchdog] fatal:", e)
        machine.reset()


start()
