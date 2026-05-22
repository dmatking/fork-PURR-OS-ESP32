import uasyncio as asyncio
import network
import utime

_BEAT_INTERVAL = 2000
_SCAN_INTERVAL = 30000


class WifiModule:
    NAME = 'wifi'

    def __init__(self, core):
        self._core = core
        self._q = core.subscribe('wifi')
        self._sta = network.WLAN(network.STA_IF)
        self._ap  = network.WLAN(network.AP_IF)
        self._ap.active(False)

    async def run(self):
        self._sta.active(True)
        print("[wifi] init OK, MAC:", self._sta.config('mac').hex(':'))
        self._publish_status()

        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._msg_loop()
        finally:
            beat_task.cancel()

    async def _msg_loop(self):
        while True:
            msg = await self._q.get()
            kind = msg.get('type')
            if kind == 'connect':
                await self._connect(msg.get('ssid'), msg.get('password'))
            elif kind == 'disconnect':
                self._sta.disconnect()
                self._publish_status()
            elif kind == 'status':
                self._publish_status()
            elif kind == 'scan':
                nets = self._sta.scan()
                self._core.publish('wifi.scan', {'nets': nets})

    async def _connect(self, ssid, password):
        if not ssid:
            return
        print("[wifi] connecting to:", ssid)
        self._sta.connect(ssid, password)
        deadline = utime.ticks_add(utime.ticks_ms(), 15000)
        while not self._sta.isconnected():
            if utime.ticks_diff(deadline, utime.ticks_ms()) <= 0:
                print("[wifi] connect timeout")
                self._publish_status()
                return
            await asyncio.sleep_ms(500)
        print("[wifi] connected:", self._sta.ifconfig())
        self._publish_status()

    def _publish_status(self):
        self._core.publish('wifi.status', {
            'connected': self._sta.isconnected(),
            'ifconfig': self._sta.ifconfig() if self._sta.isconnected() else None,
        })

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
