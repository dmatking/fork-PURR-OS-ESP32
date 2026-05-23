"""
LoRa module — SX1262 driver + Meshtastic packet layer + kern virtual node.

Layer 1: SX1262 HAL     — BUSY/DIO1 handling, init, RX polling, async TX
Layer 2: Radio config   — Meshtastic LongFast: SF11, BW 250 kHz, CR 4/5, 915 MHz
Layer 3: Mesh packet    — 14-byte Meshtastic header, protobuf Data payload, hop flood
Layer 4: Encryption     — AES-256-CTR per-channel PSK (Meshtastic-compatible)
Layer 5: Kern node      — Virtual second Meshtastic node owned by KITT.
                          Appears on the mesh as a separate device. Accepts admin config
                          (SetChannel, PSK), responds to NodeInfo requests, and executes
                          remote shell commands delivered as text messages.
Layer 6: KITT module    — IPC channels, heartbeat, emulator spoof

IPC:
  lora          subscribe  {type:'send', to:int, text:str}
                           {type:'status'}
                           {type:'meshme', count:int}
                           {type:'kern_sendpsk', to_node:int}
  lora.rx       publish    {from_node:int, to:int, id:int, hops:int, portnum:int,
                            text:str|None, rssi:int, snr:float}
  lora.tx       publish    {status:'sent'|'error', error:str}
  lora.status   publish    {ready:bool, spoof:bool, node:int, kern_node:int}
  lora.kern.rx  publish    {from_node:int, portnum:int, cmd:str|None, rssi:int, snr:float}
  lora.kern.req publish    {type:'psk_request', from_node:int}
  explorer.tray publish    {lora:bool}
"""

import uasyncio as asyncio
import machine
import struct
import utime
import ubinascii

_BEAT_INTERVAL     = 2000
_RX_POLL_MS        = 50
_SPOOF_RX_MS       = 45_000
_NODEINFO_INTERVAL = 300_000   # broadcast kern NodeInfo every 5 min
_MESHME_DELAY_MS   = 1200      # ms between meshme test packets

# ── SX1262 opcodes ────────────────────────────────────────────────────────────

_OP_STANDBY      = 0x80
_OP_PKT_TYPE     = 0x8A
_OP_RF_FREQ      = 0x86
_OP_PA_CFG       = 0x95
_OP_TX_PARAMS    = 0x8E
_OP_MOD_PARAMS   = 0x8B
_OP_PKT_PARAMS   = 0x8C
_OP_DIO_IRQ      = 0x08
_OP_SET_RX       = 0x82
_OP_SET_TX       = 0x83
_OP_WRITE_BUF    = 0x0E
_OP_READ_BUF     = 0x1E
_OP_GET_STATUS   = 0xC0
_OP_GET_IRQ      = 0x12
_OP_CLR_IRQ      = 0x02
_OP_GET_RX_BUF   = 0x13
_OP_GET_PKT_STAT = 0x14
_OP_WRITE_REG    = 0x0D

_IRQ_TX_DONE = 0x0001
_IRQ_RX_DONE = 0x0002
_IRQ_TIMEOUT = 0x0200

_REG_SYNC_WORD = 0x0740

# ── Meshtastic radio parameters (LongFast, US 915 MHz) ───────────────────────

_FREQ_HZ  = 915_000_000
_SF       = 11
_BW       = 0x05   # 250 kHz
_CR       = 0x01   # 4/5
_PREAMBLE = 16
_TX_PWR   = 22

# ── Meshtastic packet format ──────────────────────────────────────────────────

_HDR_FMT      = '<IIIBB'
_HDR_LEN      = 14
_BROADCAST    = 0xFFFFFFFF
_DEFAULT_HOPS = 3

# ── Meshtastic portnums ───────────────────────────────────────────────────────

_PORT_TEXT     = 1    # TEXT_MESSAGE_APP
_PORT_NODEINFO = 4    # NODEINFO_APP
_PORT_ADMIN    = 67   # ADMIN_APP

# ── Encryption ────────────────────────────────────────────────────────────────
# AES-256-CTR, Meshtastic-compatible.
# Nonce: pkt_id(4B LE) + 0x00000000(4B) + from_node(4B LE) + 0x00000000(4B)
# Key:   channel PSK — 16 or 32 bytes. Single byte 0x01 ("AQ==") expands to
#        the well-known default key below.
# Verify _DEFAULT_PSK_BYTES against meshtastic/firmware:src/mesh/CryptoEngine.cpp
# if interop with real Meshtastic nodes fails.

_DEFAULT_PSK_BYTES = bytes([
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x08, 0x59,
    0xc2, 0xd3, 0xcf, 0x3d, 0x33, 0x6e, 0x48, 0x68,
    0x14, 0xcf, 0x42, 0x40, 0xb7, 0xf4, 0xbc, 0x2d,
    0x3f, 0x7f, 0x0b, 0x68, 0xe2, 0xe4, 0xf9, 0xd3,
])


def _expand_psk(psk_b64):
    """Decode base64 PSK and expand to a usable AES key (16 or 32 bytes)."""
    if not psk_b64:
        return None
    try:
        raw = ubinascii.a2b_base64(psk_b64)
    except Exception:
        return None
    if len(raw) == 1 and raw[0] == 1:
        return _DEFAULT_PSK_BYTES
    if len(raw) in (16, 32):
        return bytes(raw)
    return (bytes(raw) + bytes(32))[:32]


def _mesh_nonce(pkt_id, from_node):
    """16-byte AES-CTR nonce: pkt_id(4B LE) | 0(4B) | from_node(4B LE) | 0(4B)."""
    return struct.pack('<II', pkt_id, 0) + struct.pack('<II', from_node, 0)


def _aes_ctr(key, nonce16, data):
    """AES-CTR using ucryptolib AES-ECB. Encrypt and decrypt are the same operation."""
    try:
        import ucryptolib
    except ImportError:
        return data   # no crypto available — pass through unchanged
    nblocks = (len(data) + 15) // 16
    ctr = bytearray(nonce16)
    keystream_buf = bytearray(nblocks * 16)
    for i in range(nblocks):
        keystream_buf[i * 16:(i + 1) * 16] = ctr
        for k in range(15, -1, -1):   # increment big-endian counter (byte 15 = LSB)
            ctr[k] = (ctr[k] + 1) & 0xFF
            if ctr[k]:
                break
    keystream = ucryptolib.aes(key, 1).encrypt(bytes(keystream_buf))
    out = bytearray(len(data))
    for i in range(len(data)):
        out[i] = data[i] ^ keystream[i]
    return bytes(out)


# ── Protobuf helpers ──────────────────────────────────────────────────────────

def _get_node_id():
    try:
        import network
        mac = network.WLAN(network.STA_IF).config('mac')
        return struct.unpack('>I', mac[2:])[0]
    except Exception:
        uid = machine.unique_id()
        return struct.unpack('>I', uid[:4])[0]


def _varint_enc(v):
    out = bytearray()
    while v > 0x7F:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    out.append(v)
    return bytes(out)


def _varint_dec(data, i):
    v = 0; shift = 0
    while i < len(data):
        b = data[i]; i += 1
        v |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return v, i


def _pb_bytes(field, data):
    """Protobuf length-delimited field encoding."""
    tag = (field << 3) | 2
    return bytes([tag]) + _varint_enc(len(data)) + data


def _encode_data(portnum, payload):
    """Encode a Meshtastic Data protobuf: portnum(field 1) + payload(field 2)."""
    if isinstance(payload, str):
        payload = payload.encode()
    return bytes([0x08]) + _varint_enc(portnum) + _pb_bytes(2, payload)


def _encode_text(text):
    payload = text.encode() if isinstance(text, str) else text
    return _encode_data(_PORT_TEXT, payload)


def _decode_packet(data):
    """Decode a Meshtastic Data protobuf. Returns (portnum, payload_bytes)."""
    i = 0
    portnum = 0
    payload = b''
    try:
        while i < len(data):
            tag = data[i]; i += 1
            field, wire = tag >> 3, tag & 0x07
            if wire == 0:
                v, i = _varint_dec(data, i)
                if field == 1:
                    portnum = v
            elif wire == 2:
                n, i = _varint_dec(data, i)
                chunk = data[i:i + n]; i += n
                if field == 2:
                    payload = bytes(chunk)
            else:
                break
    except Exception:
        pass
    return portnum, payload


def _encode_nodeinfo(node_id, long_name='PURR-KERN', short_name='PUKN'):
    """Encode a Meshtastic User protobuf for NodeInfo."""
    id_str = '!{:08x}'.format(node_id).encode()
    lng    = long_name.encode() if isinstance(long_name, str) else long_name
    shrt   = (short_name[:4]).encode() if isinstance(short_name, str) else short_name[:4]
    return _pb_bytes(1, id_str) + _pb_bytes(2, lng) + _pb_bytes(3, shrt)


def _parse_admin_set_channel(data):
    """
    Parse AdminMessage for SetChannel (field 16).
    Returns (channel_index, psk_bytes) or None.
    This lets the kern node auto-accept PSK changes pushed by a Meshtastic app.
    """
    try:
        i = 0
        chan_msg = None
        while i < len(data):
            tag = data[i]; i += 1
            field, wire = tag >> 3, tag & 0x07
            if wire == 0:
                _, i = _varint_dec(data, i)
            elif wire == 2:
                n, i = _varint_dec(data, i)
                chunk = data[i:i + n]; i += n
                if field == 16:   # set_channel
                    chan_msg = bytes(chunk)
            else:
                break

        if not chan_msg:
            return None

        # Parse Channel: field 1 = index, field 2 = ChannelSettings, field 3 = role
        ch_idx = 0
        settings_msg = None
        i = 0
        while i < len(chan_msg):
            tag = chan_msg[i]; i += 1
            field, wire = tag >> 3, tag & 0x07
            if wire == 0:
                v, i = _varint_dec(chan_msg, i)
                if field == 1:
                    ch_idx = v
            elif wire == 2:
                n, i = _varint_dec(chan_msg, i)
                chunk = chan_msg[i:i + n]; i += n
                if field == 2:
                    settings_msg = bytes(chunk)
            else:
                break

        if not settings_msg:
            return None

        # Parse ChannelSettings: field 2 = psk
        psk = None
        i = 0
        while i < len(settings_msg):
            tag = settings_msg[i]; i += 1
            field, wire = tag >> 3, tag & 0x07
            if wire == 0:
                _, i = _varint_dec(settings_msg, i)
            elif wire == 2:
                n, i = _varint_dec(settings_msg, i)
                chunk = settings_msg[i:i + n]; i += n
                if field == 2:
                    psk = bytes(chunk)
            else:
                break

        return (ch_idx, psk) if psk else None
    except Exception:
        return None


# ── SX1262 HAL ────────────────────────────────────────────────────────────────
# Every SPI transaction must wait for BUSY low first.

class _SX1262:

    def __init__(self, spi, cs, rst, busy, dio1):
        self._spi  = spi
        self._cs   = cs
        self._rst  = rst
        self._busy = busy
        self._dio1 = dio1

    def _wait_busy(self, ms=100):
        t = utime.ticks_add(utime.ticks_ms(), ms)
        while self._busy.value():
            if utime.ticks_diff(t, utime.ticks_ms()) <= 0:
                raise OSError('SX1262 BUSY timeout')

    def _cmd(self, op, params=b'', rx_n=0):
        self._wait_busy()
        buf = bytearray([op]) + bytearray(params) + bytearray(rx_n)
        out = bytearray(len(buf))
        self._cs.value(0)
        self._spi.write_readinto(buf, out)
        self._cs.value(1)
        return out[1 + len(params):]

    def _write_reg(self, addr, data):
        self._cmd(_OP_WRITE_REG, bytes([addr >> 8, addr & 0xFF]) + bytes(data))

    def reset(self):
        self._rst.value(0); utime.sleep_ms(1)
        self._rst.value(1); utime.sleep_ms(10)
        self._wait_busy()

    def get_status(self):
        return self._cmd(_OP_GET_STATUS, b'', 1)[0]

    def init(self):
        self._cmd(_OP_STANDBY, b'\x00')
        self._cmd(_OP_PKT_TYPE, b'\x01')
        fv = int(_FREQ_HZ / 32e6 * (1 << 25))
        self._cmd(_OP_RF_FREQ, struct.pack('>I', fv))
        self._cmd(_OP_PA_CFG, b'\x04\x07\x00\x01')
        self._cmd(_OP_TX_PARAMS, bytes([_TX_PWR, 0x04]))
        self._cmd(_OP_MOD_PARAMS, bytes([_SF, _BW, _CR, 0x00]))
        self._cmd(_OP_PKT_PARAMS,
                  struct.pack('>H', _PREAMBLE) + b'\x00\xFF\x01\x00')
        self._write_reg(_REG_SYNC_WORD, [0x14, 0x24])   # Meshtastic private
        mask = _IRQ_TX_DONE | _IRQ_RX_DONE | _IRQ_TIMEOUT
        self._cmd(_OP_DIO_IRQ,
                  struct.pack('>H', mask) + struct.pack('>H', mask) + b'\x00\x00\x00\x00')
        self.start_rx()

    def start_rx(self):
        self._cmd(_OP_SET_RX, b'\xFF\xFF\xFF')

    def start_tx(self, data):
        self._cmd(_OP_STANDBY, b'\x00')
        self._cmd(_OP_WRITE_BUF, bytes([0x00]) + data)
        self._cmd(_OP_PKT_PARAMS,
                  struct.pack('>H', _PREAMBLE) + bytes([0x00, len(data), 0x01, 0x00]))
        self._cmd(_OP_SET_TX, b'\x03\xE8\x00')

    def get_irq(self):
        r = self._cmd(_OP_GET_IRQ, b'\x00', 2)
        return (r[0] << 8) | r[1]

    def clr_irq(self, mask=0xFFFF):
        self._cmd(_OP_CLR_IRQ, struct.pack('>H', mask))

    def read_packet(self):
        s       = self._cmd(_OP_GET_RX_BUF, b'\x00', 2)
        pkt_len, buf_off = s[0], s[1]
        raw     = bytes(self._cmd(_OP_READ_BUF, bytes([buf_off, 0x00]), pkt_len))
        ps      = self._cmd(_OP_GET_PKT_STAT, b'\x00', 3)
        rssi    = -(ps[0] >> 1)
        snr     = ps[1] / 4.0 if ps[1] < 128 else (ps[1] - 256) / 4.0
        return raw, rssi, snr


# ── KITT module ───────────────────────────────────────────────────────────────

class LoraModule:
    NAME = 'lora'

    def __init__(self, core):
        self._core      = core
        self._q         = core.subscribe('lora')
        self._radio     = None
        self._ready     = False
        self._spoof     = False
        self._txing     = False
        self._node      = _get_node_id()
        self._seq       = 0
        self._seen      = []
        self._psk       = None      # AES-256-CTR key (bytes) or None
        self._kern_node = None      # virtual kern node ID
        self._kern_q    = []        # (from_n, pkt_id, portnum, payload, rssi, snr)

    async def run(self):
        self._init_hw()
        self._publish_status()
        print('[lora] node={:08X} kern={:08X}'.format(
            self._node, self._kern_node or 0),
            '(spoof)' if self._spoof else ('ready' if self._ready else 'FAIL'))
        print('[lora] encryption:', 'ON' if self._psk else 'OFF')

        beat_task     = asyncio.create_task(self._heartbeat())
        kern_task     = asyncio.create_task(self._kern_loop())
        nodeinfo_task = asyncio.create_task(self._nodeinfo_loop())
        rx_task       = asyncio.create_task(
            self._spoof_rx_loop() if self._spoof else self._rx_loop()
        )
        try:
            await self._msg_loop()
        finally:
            beat_task.cancel()
            kern_task.cancel()
            nodeinfo_task.cancel()
            rx_task.cancel()

    # ── Hardware init ─────────────────────────────────────────────────────────

    def _init_hw(self):
        cfg  = self._load_cfg()
        pins = cfg.get('lora_pins', {})

        # PSK
        psk_b64 = cfg.get('lora_psk', '')
        self._psk = _expand_psk(psk_b64) if psk_b64 else None

        # Kern node ID: user-set or derived from main node with top-half flip
        kern = cfg.get('kern_node_id')
        self._kern_node = (kern & 0xFFFFFFFF) if kern else ((self._node ^ 0xFFFF0000) & 0xFFFFFFFF)

        try:
            spi  = machine.SPI(
                1, baudrate=2_000_000, polarity=0, phase=0,
                sck =machine.Pin(pins['sck']),
                mosi=machine.Pin(pins['mosi']),
                miso=machine.Pin(pins['miso']),
            )
            cs   = machine.Pin(pins['cs'],   machine.Pin.OUT, value=1)
            rst  = machine.Pin(pins['rst'],  machine.Pin.OUT, value=1)
            busy = machine.Pin(pins['busy'], machine.Pin.IN)
            dio1 = machine.Pin(pins['dio1'], machine.Pin.IN)

            radio = _SX1262(spi, cs, rst, busy, dio1)
            radio.reset()

            status = radio.get_status()
            if status == 0x00:
                self._spoof = True
                self._ready = True
                return
            if status == 0xFF:
                return

            radio.init()
            self._radio = radio
            self._ready = True

        except Exception as e:
            print('[lora] hw init failed:', e)

    # ── RX loop ───────────────────────────────────────────────────────────────

    async def _rx_loop(self):
        while True:
            await asyncio.sleep_ms(_RX_POLL_MS)
            if self._txing or not self._radio:
                continue
            try:
                irq = self._radio.get_irq()
                if irq & _IRQ_RX_DONE:
                    self._radio.clr_irq()
                    raw, rssi, snr = self._radio.read_packet()
                    self._handle_rx(raw, rssi, snr)
                elif irq & _IRQ_TIMEOUT:
                    self._radio.clr_irq()
                    self._radio.start_rx()
            except Exception as e:
                print('[lora] rx error:', e)

    def _handle_rx(self, raw, rssi, snr):
        if len(raw) < _HDR_LEN:
            return
        to_n, from_n, pkt_id, flags, ch = struct.unpack(_HDR_FMT, raw[:_HDR_LEN])
        hops = flags & 0x07

        if not self._seen_add(pkt_id):
            return

        raw_payload = raw[_HDR_LEN:]

        # Decrypt (if PSK configured). Keep raw_payload for flood re-broadcast.
        if self._psk and raw_payload:
            dec_payload = _aes_ctr(self._psk, _mesh_nonce(pkt_id, from_n), raw_payload)
        else:
            dec_payload = raw_payload

        portnum, pkt_payload = _decode_packet(dec_payload)

        # Route packets addressed to the kern virtual node
        if self._kern_node and to_n == self._kern_node:
            self._kern_q.append((from_n, pkt_id, portnum, pkt_payload, rssi, snr))
            return

        # Decode text for normal channel publish
        text = None
        if portnum == _PORT_TEXT:
            try:
                text = pkt_payload.decode()
            except Exception:
                pass

        self._core.publish('lora.rx', {
            'from_node': from_n, 'to': to_n, 'id': pkt_id,
            'hops': hops, 'portnum': portnum, 'text': text,
            'rssi': rssi, 'snr': snr,
        })

        # Re-flood broadcasts with original (encrypted) payload
        if hops > 1 and to_n == _BROADCAST:
            reheader = struct.pack(_HDR_FMT, to_n, from_n, pkt_id, (hops - 1) & 0x07, ch)
            asyncio.create_task(self._flood_tx(reheader + raw_payload))

    # ── Common TX path ────────────────────────────────────────────────────────

    async def _transmit(self, to_node, from_id, proto, hops=_DEFAULT_HOPS):
        """Encrypt, frame, and send one packet. Returns True on success."""
        self._seq  += 1
        pkt_id      = (from_id & 0xFFFF0000) | (self._seq & 0xFFFF)
        payload     = proto

        if self._psk and payload:
            payload = _aes_ctr(self._psk, _mesh_nonce(pkt_id, from_id), payload)

        header = struct.pack(_HDR_FMT, to_node, from_id, pkt_id, hops & 0x07, 0)
        pkt    = header + payload
        self._seen_add(pkt_id)

        if self._spoof:
            return True

        if not self._ready or not self._radio:
            return False

        self._txing = True
        ok = False
        try:
            self._radio.start_tx(pkt)
            deadline = utime.ticks_add(utime.ticks_ms(), 4000)
            while True:
                irq = self._radio.get_irq()
                if irq & (_IRQ_TX_DONE | _IRQ_TIMEOUT):
                    self._radio.clr_irq()
                    ok = True
                    break
                if utime.ticks_diff(deadline, utime.ticks_ms()) <= 0:
                    break
                await asyncio.sleep_ms(10)
        except Exception as e:
            print('[lora] tx error:', e)
        finally:
            self._txing = False
            if self._radio:
                self._radio.start_rx()
        return ok

    async def _flood_tx(self, pkt):
        """Re-broadcast a packet (already framed with header + original payload)."""
        if self._txing or not self._ready or not self._radio:
            return
        self._txing = True
        try:
            self._radio.start_tx(pkt)
            deadline = utime.ticks_add(utime.ticks_ms(), 4000)
            while True:
                irq = self._radio.get_irq()
                if irq & (_IRQ_TX_DONE | _IRQ_TIMEOUT):
                    self._radio.clr_irq()
                    break
                if utime.ticks_diff(deadline, utime.ticks_ms()) <= 0:
                    break
                await asyncio.sleep_ms(10)
        except Exception as e:
            print('[lora] flood error:', e)
        finally:
            self._txing = False
            if self._radio:
                self._radio.start_rx()

    # ── Outbound message loop ─────────────────────────────────────────────────

    async def _msg_loop(self):
        while True:
            msg = await self._q.get()
            t = msg.get('type')
            if t == 'status':
                self._publish_status()
            elif t == 'send':
                await self._do_send(msg)
            elif t == 'meshme':
                count = max(1, int(msg.get('count', 1)))
                asyncio.create_task(self._do_meshme(count))
            elif t == 'kern_reply':
                to_node = msg.get('to')
                text    = msg.get('text', '')
                if to_node and text and self._kern_node:
                    await self._transmit(to_node, self._kern_node,
                                         _encode_text(text), hops=1)
            elif t == 'kern_setpsk':
                key = _expand_psk(msg.get('key', ''))
                if key:
                    self._psk = key
                    print('[lora] PSK updated via netboot')
            elif t == 'kern_sendpsk':
                to_node = msg.get('to_node')
                if to_node and self._psk:
                    b64 = ubinascii.b2a_base64(self._psk).decode().strip()
                    await self._transmit(to_node, self._kern_node,
                                         _encode_text('PSK:' + b64), hops=1)
                    print('[lora] sent PSK to {:08X}'.format(to_node))

    async def _do_send(self, msg):
        to_node = msg.get('to', _BROADCAST)
        text    = msg.get('text', '')
        ok = await self._transmit(to_node, self._node, _encode_text(text))
        self._core.publish('lora.tx', {'status': 'sent' if ok else 'error',
                                        'error': '' if ok else 'tx failed'})

    # ── meshme: send N test broadcasts from the kern node ────────────────────

    async def _do_meshme(self, count):
        print('[lora] meshme: sending {} packet{}'.format(count, 's' if count != 1 else ''))
        for i in range(count):
            text = 'PURR {}/{}'.format(i + 1, count)
            await self._transmit(_BROADCAST, self._kern_node, _encode_text(text))
            if i < count - 1:
                await asyncio.sleep_ms(_MESHME_DELAY_MS)
        print('[lora] meshme: done')

    # ── Kern node loop ────────────────────────────────────────────────────────
    # Processes packets addressed to the kern virtual node.

    async def _kern_loop(self):
        while True:
            await asyncio.sleep_ms(100)
            if not self._kern_q or self._txing:
                continue
            from_n, pkt_id, portnum, payload, rssi, snr = self._kern_q.pop(0)
            await self._handle_kern_pkt(from_n, pkt_id, portnum, payload, rssi, snr)

    async def _handle_kern_pkt(self, from_n, pkt_id, portnum, payload, rssi, snr):
        cmd = None

        if portnum == _PORT_TEXT:
            try:
                cmd = payload.decode().strip()
            except Exception:
                cmd = None
            # execution and reply handled by remoteass module via lora.kern.rx

        elif portnum == _PORT_NODEINFO:
            # Another node sent its NodeInfo — respond with ours
            await self._send_nodeinfo(from_n)

        elif portnum == _PORT_ADMIN:
            # Admin message — check for SetChannel (auto-accept PSK changes)
            result = _parse_admin_set_channel(payload)
            if result:
                ch_idx, psk_bytes = result
                expanded = (bytes(psk_bytes) + bytes(32))[:32] if len(psk_bytes) < 32 else bytes(psk_bytes[:32])
                self._psk = expanded
                b64 = ubinascii.b2a_base64(self._psk).decode().strip()
                print('[lora] kern: PSK updated via admin (ch {})'.format(ch_idx))
                self._core.publish('lora.status', {'ready': self._ready,
                                                    'spoof': self._spoof,
                                                    'node':  self._node,
                                                    'kern_node': self._kern_node,
                                                    'psk_set': True})

        self._core.publish('lora.kern.rx', {
            'from_node': from_n, 'portnum': portnum,
            'cmd': cmd, 'rssi': rssi, 'snr': snr,
        })

    # ── NodeInfo broadcasts ───────────────────────────────────────────────────

    async def _nodeinfo_loop(self):
        """Periodically broadcast kern NodeInfo so the Meshtastic mesh sees it."""
        await asyncio.sleep_ms(5000)
        interval = 15_000 if self._spoof else _NODEINFO_INTERVAL  # 15s in emulator, 5min on hw
        while True:
            await self._send_nodeinfo(_BROADCAST)
            await asyncio.sleep_ms(interval)

    async def _send_nodeinfo(self, to_node):
        if not self._kern_node:
            return
        info  = _encode_nodeinfo(self._kern_node)
        proto = _encode_data(_PORT_NODEINFO, info)
        await self._transmit(to_node, self._kern_node, proto)

    # ── Spoof RX (emulator) ───────────────────────────────────────────────────
    # Builds real wire-format packets and calls _handle_rx directly so the
    # full pipeline (decryption, kern routing, portnum dispatch) is exercised
    # in the emulator — not just the IPC publish at the end.

    async def _spoof_rx_loop(self):
        _SIM_A = 0xDEADBEEF
        _SIM_B = 0xCAFEBABE

        # Cycle through: broadcast mesh traffic + kern-addressed shell commands
        _seq = [
            (_BROADCAST,      _SIM_A, _PORT_TEXT,     'Hello from sim node A'),
            (self._kern_node, _SIM_B, _PORT_TEXT,     'help'),
            (_BROADCAST,      _SIM_A, _PORT_TEXT,     'Node A mesh check'),
            (self._kern_node, _SIM_B, _PORT_TEXT,     'mem'),
            (_BROADCAST,      _SIM_B, _PORT_TEXT,     'Node B here'),
            (self._kern_node, _SIM_A, _PORT_TEXT,     'status'),
            (self._kern_node, _SIM_B, _PORT_NODEINFO, ''),  # NodeInfo → triggers kern response
        ]

        tick   = 0
        pkt_id = 0x5C000000

        while True:
            await asyncio.sleep_ms(_SPOOF_RX_MS)
            to_n, from_n, portnum, text = _seq[tick % len(_seq)]
            tick   += 1
            pkt_id += 1

            self._inject_pkt(to_n, from_n, portnum, text, pkt_id)

    def _inject_pkt(self, to_n, from_n, portnum, text, pkt_id=None):
        """Build a real wire-format packet and feed it through _handle_rx."""
        if pkt_id is None:
            pkt_id = 0x5C000000 + (len(self._seen) & 0xFFFF)

        proto = _encode_data(portnum, text) if text else _encode_data(portnum, b'')
        if self._psk and proto:
            proto = _aes_ctr(self._psk, _mesh_nonce(pkt_id, from_n), proto)

        raw = struct.pack(_HDR_FMT, to_n, from_n, pkt_id, 3, 0) + proto
        label = 'kern' if (self._kern_node and to_n == self._kern_node) else 'broadcast'
        print('[lora] SIM {} from {:08X}: {!r}'.format(label, from_n, text or '<{}>'.format(portnum)))
        self._handle_rx(raw, -85, 7.5)

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _seen_add(self, pkt_id):
        if pkt_id in self._seen:
            return False
        self._seen.append(pkt_id)
        if len(self._seen) > 32:
            self._seen.pop(0)
        return True

    def _publish_status(self):
        self._core.publish('lora.status', {
            'ready':     self._ready,
            'spoof':     self._spoof,
            'node':      self._node,
            'kern_node': self._kern_node,
        })
        self._core.publish('explorer.tray', {'lora': self._ready and not self._spoof})

    def _load_cfg(self):
        import json
        with open('/system/kernel.app/device.json') as f:
            return json.load(f)

    async def _heartbeat(self):
        while True:
            self._core.beat(self.NAME)
            await asyncio.sleep_ms(_BEAT_INTERVAL)
