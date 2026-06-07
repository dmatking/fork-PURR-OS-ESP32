import machine
import sys

# 8s gives the kernel enough time to start before NanoCore takes over WDT feeding
_HW_WDT_TIMEOUT = 8000


class Watchdog:
    def __init__(self):
        self._wdt = machine.WDT(timeout=_HW_WDT_TIMEOUT)
        print("[watchdog] HW WDT armed, timeout:", _HW_WDT_TIMEOUT, "ms")

    def run(self):
        self._wdt.feed()
        self._launch_kernel()
        # _launch_kernel blocks while the kernel event loop runs.
        # Reaching here means the kernel exited — treat as fatal.
        print("[watchdog] kernel exited unexpectedly, resetting")
        machine.reset()

    def _launch_kernel(self):
        print("[watchdog] launching kernel")
        try:
            # exec avoids the circular-import problem that `import main` would cause.
            # _wdt is passed so NanoCore can take over feeding via its heartbeat loop.
            with open('/system/kernel.app/main.py') as f:
                exec(f.read(), {'__name__': '__main__', '_wdt': self._wdt})
        except Exception as e:
            print("[watchdog] kernel launch failed:", e)
            machine.reset()


def start():
    wd = Watchdog()
    try:
        wd.run()
    except Exception as e:
        print("[watchdog] fatal:", e)
        machine.reset()


start()
