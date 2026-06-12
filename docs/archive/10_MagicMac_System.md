# MagicMac: 68k Mac Plus Emulator

MagicMac is a full-screen Mac OS System 3-7 emulator running on ESP32-S3, with seamless integration to PURR OS kernel services via an IPC bridge.

## Quick Start

### 1. Install Mac ROM

MagicMac requires a **Mac Plus ROM image (512 KB)**.

#### Obtaining the ROM
- **Legal**: Dump from an original Mac Plus (with proper tools)
- **Abandonware**: Various ROM archives online (legal gray area)
- **Cannot be included in repo** due to copyright

#### Installation
Place the ROM at: `/sdcard/magicmac/mac.rom`

### 2. Build with MagicMac Support

```bash
python3 SDK/sdk_core.py --target tdeck_plus --magicmac --build
python3 SDK/sdk_core.py --target tdeck_plus --magicmac --flash /dev/ttyACM0
```

### 3. Select Boot Mode

**On first boot:**
- Device shows boot menu (fullscreen app)
- Select "MagicMac" to enable
- Device reboots into Mac OS

**Change modes anytime:**
- Open "MagicMac" settings app (available in taskbar)
- Toggle boot mode
- Reboot

---

## Boot System

### Boot Modes

**PURR OS (Default)**
- Normal PURR OS with MiniWin UI
- All PURR apps available
- Full WiFi/BT support

**MagicMac**
- Pure Mac OS System 3-7 environment
- WiFi/BT disabled at kernel level (saves 2-3MB RAM)
- Can be re-enabled via settings app
- Bootloader at 0x10000 in factory partition

### Boot Flow

```
Power On
  ↓
KITT loads boot_mode from NVS (purr_kernel namespace)
  ↓
System startup checks boot_mode
  ├─ BOOT_MAGICMAC → Launch purr_classic (Mac emulator)
  │   ├─ Skip MiniWin
  │   ├─ Skip WiFi/BT init (memory optimization)
  │   └─ Render Mac framebuffer to display
  │
  └─ BOOT_PURR_OS → Launch normal shells (KittenUI, etc.)
      └─ Full PURR OS experience
```

---

## Configuration

### magicmac.json

Located at: `/sdcard/magicmac/magicmac.json`

```json
{
  "boot_disk": "/sdcard/magicmac/meow.dsk",
  "wifi_enabled": false,
  "bt_enabled": false,
  "autostart": false
}
```

**Fields:**
- `boot_disk`: Path to boot disk image (.dsk file)
- `wifi_enabled`: Enable WiFi from within MagicMac
- `bt_enabled`: Enable Bluetooth from within MagicMac
- `autostart`: Automatically boot into MagicMac on power-on

**Manage via:**
- MagicMac settings app (in taskbar when running PURR OS)
- Direct JSON edit on SD card

---

## Exposed Kernel API

### IPC Bridge (68k ↔ ESP32)

Mac apps can access PURR OS kernel services via memory-mapped IPC at `0xF00000` in 68k address space.

#### How It Works

1. **Mac app writes command struct** to absolute address `0xF00000`
2. **Sets status byte** to `PENDING`
3. **Espressif side**:
   - Detects write via memory bus hook
   - Dispatches command to PURR kernel
   - Writes response back to `0xF00000`
   - Sets status to `DONE`
4. **Mac app spin-polls** status byte (< 1ms round-trip)

### Available Commands

See `magicmac/CoreOS/components/lib_purr_ipc/purr_ipc.h` for full command set.

**Common operations:**
- `PURR_IPC_PING` — Verify IPC bridge is alive
- `PURR_IPC_LORA_SEND` — Transmit LoRa packet
- `PURR_IPC_LORA_RECV` — Receive LoRa packet
- `PURR_IPC_WIFI_STATUS` — Check WiFi connection
- `PURR_IPC_SD_LIST` — List SD card files
- `PURR_IPC_NOTIFY` — Send notification to PURR OS

### Mac-Side SDK

**File:** `magicmac/SDK/retro68/PurrIPC.h` and `PurrIPC.c`

**Usage in Mac App:**

```c
#include "PurrIPC.h"

// Call PURR kernel function from 68k
int result = purr_lora_send(data, len);

// Check WiFi status
bool connected = purr_wifi_connected();

// List SD card
purr_sd_file_t files[32];
int count = purr_sd_list("/", files, 32);
```

**Building Mac Apps:**

Use Retro68 toolchain to compile 68k Mac apps with PURR IPC support:

```bash
cd magicmac/SDK/retro68/apps/purr_lora_chat/
make
# Output: purr_lora_chat.bin (ready for Mac desktop)
```

---

## Hardware Configuration

### RAM Allocation

**MagicMac Mode (optimized for emulator):**
- PSRAM: 4-8 MB (configurable, device-dependent)
- Internal RAM: 48 KB reserved for IPC + kernel
- WiFi/BT disabled (no stack overhead)
- Frees ~2-3 MB vs normal PURR OS

**T-Deck Plus Specific:**
- 8MB PSRAM available
- 4MB allocated to Mac RAM
- 4MB free for OS buffers
- 240 MHz CPU

### Display

**Rendering:**
- Mac Plus native: 512×342 1bpp monochrome
- T-Deck Plus: 320×240 landscape RGB565
- Scaling: Nearest-neighbor (fullscreen fit)
- No aspect-ratio correction (acceptable distortion)
- ~49K pixels per frame

### Input

**Touch:**
- Wired to Mac ADB mouse
- Scaled from 320×240 display to 512×342 Mac coordinates
- Works with Finder, dialogs, apps

**Keyboard:**
- T-Deck Plus keyboard wired to ADB keyboard interface
- Full keyboard support (letters, symbols, special keys)

---

## Development

### Building Custom Mac Apps

**Requirements:**
- Retro68 toolchain (68k compiler)
- PURR IPC headers
- Standard Mac Toolbox

**Example: LoRa Chat Terminal**

See `magicmac/SDK/retro68/apps/purr_lora_chat/` for a complete example.

**Steps:**
1. Write 68k C code using Mac Toolbox
2. Include `PurrIPC.h` for kernel access
3. Compile with Retro68
4. Place .bin on Mac desktop
5. Double-click to launch

### Debugging

**From PURR OS side:**
```bash
# Monitor MagicMac boot
python3 SDK/sdk_core.py --target tdeck_plus --monitor

# Check IPC bridge logs
# Watch for "purr_ipc" tagged messages
```

**From Mac side:**
- Mac System Error Handler catches 68k exceptions
- PURR IPC returns error codes on failure
- Check kernel logs for dispatch errors

---

## Performance

**Frame Rate:**
- 512×342 Mac screen → 320×240 display
- Scaling: ~200 µs per frame
- Display blit: ~500 µs (display bus dependent)
- Net: ~60 FPS target (achievable on 240 MHz ESP32-S3)

**IPC Latency:**
- Command dispatch: < 1 ms
- Most operations complete in < 100 µs
- Suitable for real-time I/O (LoRa, WiFi)

---

## Troubleshooting

### Device doesn't boot into MagicMac

**Check:**
1. Mac ROM at `/sdcard/magicmac/mac.rom` (exactly 512 KB)
2. `magicmac.json` exists with valid JSON
3. Boot mode setting in settings app (check "MagicMac" is selected)

### Mac desktop shows scrambled/corrupted display

**Common causes:**
- ROM checksum mismatch (corrupted file)
- PSRAM initialization failed (device-specific)
- Display driver not fully initialized

**Fix:**
1. Verify ROM file is exactly 512 KB: `ls -l /sdcard/magicmac/mac.rom`
2. Try booting PURR OS first (verify display works)
3. Check build output for PSRAM init errors

### Touch doesn't work

**Note:** Touch input wiring is still in progress (TODO in purr_classic.cpp).

---

## References

- `magicmac/README.md` — Architecture overview
- `magicmac/CoreOS/components/lib_purr_ipc/purr_ipc.h` — IPC command definitions
- `magicmac/SDK/retro68/PurrIPC.h` — Mac-side SDK
- `magicmac/Shells/purr_classic/purr_classic.cpp` — Emulator entry point
