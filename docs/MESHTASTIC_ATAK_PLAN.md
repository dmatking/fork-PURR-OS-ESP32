# PURR OS — Meshtastic + ATAK Gateway Plan

## Overview

PURR OS targets Meshtastic protocol compatibility on LoRa hardware (SX1262 on Heltec V3, T-Deck). The goal is a native mesh node that:

1. Interoperates with standard Meshtastic firmware nodes
2. Exposes a local ATAK/CoT gateway over UDP for TAK clients
3. Is entirely walled behind SDK picker flags so builds that don't need mesh don't pay the cost

**CC1101 note:** The LilyGo T-Embed CC1101 uses FSK radio, not LoRa. It **cannot** participate in Meshtastic (which requires LoRa chirp spread spectrum). CC1101 is a separate radio stack (`drv_cc1101`) for sub-GHz FSK comms.

---

## What Is Already Implemented (`purr_mesh.cpp`)

### Phase 1 — Radio Layer ✅
- SX1262 configured for **LONG_FAST** preset: SF11, BW250kHz, CR4/5, sync=0x2B
- Region-aware frequency selection (US/EU/AU/JP/CN)
- Default channel PSK (Meshtastic shorthand "1" → 16-byte key)
- AES-256-CTR payload encryption/decryption (mbedtls)

### Phase 2 — Packet Handling ✅
- nanopb-encoded `MeshPacket` / `Data` protobuf encode + decode
- Flooding router with hop-limit decrement and re-broadcast
- Dedup ring buffer (32 slots, by `from`+`packet_id`)
- Node table (up to 16 nodes, tracks RSSI + last-seen time)
- `TEXT_MESSAGE_APP` send + receive
- `NODEINFO_APP` broadcast every 10 minutes
- Public API: `purr_mesh_init()`, `purr_mesh_send_text()`, `purr_mesh_set_rx_callback()`

---

## Remaining Phases

### Phase 3 — Position + Telemetry
**SDK flag:** `PURR_ENABLE_MESH_POSITION`

- Encode and broadcast `POSITION_APP` packets with GPS or manual lat/lon
- Store received positions in the node table alongside RSSI
- KittenUI LORA screen: show node ID + last-heard node positions

Implementation notes:
- `meshtastic/position.pb.h` is already available via `lib_mesh_pb`
- KITT has no GPS driver yet; start with static/manual position from NVS
- Broadcast interval: 30s when moving (TBD), 300s static

### Phase 4 — Admin + Config Exchange
**SDK flag:** `PURR_ENABLE_MESH_ADMIN`

- `ADMIN_APP` portnum: read/write channel config, set node short name
- Store local config in NVS (`mesh_cfg` namespace)
- SDK wizard: expose `mesh_name`, `mesh_region`, `mesh_psk` fields

### Phase 5 — ATAK / CoT Gateway
**SDK flag:** `PURR_ENABLE_ATAK`

This is the bridge mode that forwards Meshtastic mesh traffic to TAK clients (ATAK/WinTAK/iTAK) as Cursor-on-Target (CoT) XML over UDP.

Architecture:
```
[LoRa Mesh Nodes]
       |
  SX1262 RX
       |
  purr_mesh.cpp  ←→  atak_gateway.cpp
       |
  WiFi UDP multicast → 239.2.3.1:6969  (TAK multicast)
  or   WiFi UDP unicast → configured ATAK server IP
```

CoT format (minimum viable):
```xml
<event version="2.0" uid="PURR-{node_id_hex}" type="a-f-G-U-C"
       time="{ISO8601}" start="{ISO8601}" stale="{ISO8601+5min}"
       how="m-g">
  <point lat="{lat}" lon="{lon}" hae="0" ce="50" le="50"/>
  <detail>
    <contact callsign="{short_name}"/>
  </detail>
</event>
```

Files to create:
- `CoreOS/system/kernel/modules/atak_gateway.cpp/.h`
- `CoreOS/system/kernel/modules/atak_gateway.h` exposes:
  - `atak_gateway_init(const char* server_ip, uint16_t port)`
  - `atak_gateway_push_position(uint32_t node_id, float lat, float lon, const char* callsign)`
  - `atak_gateway_update()` — called from KITT::update() when PURR_ENABLE_ATAK

Dependencies: `esp_wifi` (already a REQUIRES), no extra libs needed — CoT is plain XML via `sprintf`.

### Phase 6 — Store-and-Forward
**SDK flag:** `PURR_ENABLE_MESH_STORE_FWD`

- Buffer up to N messages in SPIFFS when destination is unreachable
- Replay when node comes back into range
- Enabled only on devices with ≥8MB flash (Heltec = 8MB — marginal; T-Deck = 16MB = comfortable)

---

## SDK Picker Flags Summary

| Flag | Default | What it adds |
|------|---------|-------------|
| `PURR_ENABLE_MESH` | 0 | Core mesh stack (Phase 1-2), requires LORA |
| `PURR_ENABLE_MESH_POSITION` | 0 | GPS/position packets, node map |
| `PURR_ENABLE_MESH_ADMIN` | 0 | Channel config exchange via ADMIN_APP |
| `PURR_ENABLE_ATAK` | 0 | CoT UDP gateway to TAK clients, requires MESH |
| `PURR_ENABLE_MESH_STORE_FWD` | 0 | Store-and-forward buffering, requires MESH |

All of these need to be added to:
1. `CoreOS/main/CMakeLists.txt` — `set(PURR_ENABLE_X 0 CACHE STRING ...)`
2. `SDK/sdk_core.py` — `MODULES` list with appropriate target restrictions
3. `CoreOS/main/CMakeLists.txt` PURR_DEFS block — `if(PURR_ENABLE_X) list(APPEND PURR_DEFS PURR_HAS_X=1) endif()`

---

## Compatible Hardware

| Device | Radio | Meshtastic | ATAK GW |
|--------|-------|------------|---------|
| Heltec WiFi LoRa 32 V3 | SX1262 | ✅ | ✅ (via WiFi) |
| LilyGo T-Deck | SX1262 | ✅ | ✅ |
| LilyGo T-Deck Plus | SX1262 | ✅ | ✅ |
| LilyGo T-Embed CC1101 | CC1101 (FSK) | ❌ Not LoRa | ❌ |
| CYD variants | None | ❌ | ❌ |

---

## Protocol Reference

- Meshtastic protobuf schema: `CoreOS/components/lib_mesh_pb/meshtastic/`
- AES-256-CTR nonce layout: `[4-byte packet_id][8-byte from_node][4-byte 0x00]`
- LONG_FAST radio: SF11, BW250, CR4/5, sync=0x2B, preamble=16
- Packet wire format: `[4B magic][16B IV][N bytes ciphertext]` where magic = `0x2B` broadcast flag byte from Meshtastic header
- CoT TAK multicast: `239.2.3.1:6969` (standard TAK SA multicast group)
- CoT TAK unicast: port `8087` (TCP) or `4242` (UDP) — user-configurable

---

## Current Status

`purr_mesh.cpp` is ~400 lines, covering Phases 1-2 fully. The mesh runs as a FreeRTOS task on core 0 (separate from the UI task on core 1). Tested interoperability with Meshtastic Android app nodes is planned but not yet verified — the encoding matches the Meshtastic spec by inspection.
