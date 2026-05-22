import uasyncio as asyncio
import machine
import utime

_BEAT_INTERVAL = 2000

# SX1262 register addresses
_REG_STATUS = 0xC0


class LoraModule:
    NAME = 'lora'

    def __init__(self, core):
        self._core = core
        self._q = core.subscribe('lora')
        self._spi = None
        self._cs = None
        self._rst = None
        self._ready = False

    async def run(self):
        self._init_hw()
        print("[lora] init", "OK" if self._ready else "FAIL")
        self._core.publish('lora.status', {'ready': self._ready})

        beat_task = asyncio.create_task(self._heartbeat())
        try:
            await self._msg_loop()
        finally:
            beat_task.cancel()

    def _init_hw(self):
        cfg = self._load_cfg()
        pins = cfg.get('lora_pins', {})
        try:
            self._spi = machine.SPI(
                1,
                baudrate=2000000,
                polarity=0,
                phase=0,
                sck=machine.Pin(pins.get('sck', 9)),
                mosi=machine.Pin(pins.get('mosi', 10)),
                miso=machine.Pin(pins.get('miso', 11)),
            )
            self._cs  = machine.Pin(pins.get('cs', 8),  machine.Pin.OUT, value=1)
            self._rst = machine.Pin(pins.get('rst', 12), machine.Pin.OUT, value=1)

            # Hard reset SX1262
            self._rst.value(0)
            utime.sleep_ms(10)
            self._rst.value(1)
            utime.sleep_ms(10)

            # Read status byte — SX1262 returns 0x22 on a clean boot
            status = self._cmd(_REG_STATUS, 1)
            self._ready = (status[0] != 0xFF)  # 0xFF means no device
        except Exception as e:
            print("[lora] hw init failed:", e)
            self._ready = False

    def _cmd(self, opcode, rx_len=0, data=b''):
        buf = bytearray(1 + len(data) + rx_len)
        buf[0] = opcode
        buf[1:1+len(data)] = data
        out = bytearray(len(buf))
        self._cs.value(0)
        self._spi.write_readinto(buf, out)
        self._cs.value(1)
        return out[1 + len(data):]

    def _load_cfg(self):
        import json
        with open('/system/kernel.app/device.json') as f:
            return json.load(f)

    async def _msg_loop(self):
        while True:
            msg = await self._q.get()
            kind = msg.get('type')
            if kind == 'status':
                self._core.publish('lora.status', {'ready': self._ready})
            # Future: send, receive, frequency config

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
