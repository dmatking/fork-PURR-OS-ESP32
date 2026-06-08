# MagicMac

MagicMac is a special-edition PURR OS shell that runs Mac OS System 3 as the home screen using the umac Mac Plus emulator. The Mac Finder acts as the app launcher. Apps built with Retro68 run natively inside the emulator and can talk to the PURR OS kernel (LoRa, WiFi, Bluetooth, notifications) through a memory-mapped IPC window.

This is a full-screen shell — it replaces the MiniWin window manager entirely and owns the display and touch for the lifetime of the session, the same way the explorer or blackberry shells do.

---

## How it works

```
CYD display / touch
       |
  purr_classic (Shells/purr_classic/)
       |
  drv_umac  ----  umac + Musashi 68k emulator
       |                  |
  lib_purr_ipc     68k address space
       |            0xF00000 = IPC window
  PURR kernel               |
  (LoRa / WiFi / BT)   Mac app writes here
                        ESP32 dispatches
                        response written back
```

When a Mac app wants to use a PURR kernel service it writes a command struct to absolute 68k address `0xF00000` and sets the status byte to `PENDING`. The ESP32 side sees the write (via a hook in the emulator), dispatches the call, writes the response, and sets status to `DONE`. The Mac app spin-polls the status byte — the whole round-trip is under 1 ms for most calls.

---

## Project structure

```
magicmac/
  CoreOS/components/
    drv_umac/           umac + Musashi IDF component
      umac_core.h       public API (init, start, mouse event, frame callback)
      umac_core.c       implementation + PSRAM/heap ROM and RAM allocation
      CMakeLists.txt

    lib_purr_ipc/       IPC bridge between 68k address space and PURR kernel
      purr_ipc.h        command IDs, frame struct, ESP32-side API
      purr_ipc.c        dispatch loop, LoRa/WiFi/BT/notification handlers
      CMakeLists.txt

  Shells/purr_classic/  Full-screen shell entry point
    purr_classic.h      purr_classic_start() declaration
    purr_classic.cpp    wires touch, display, launches emulator + IPC tasks
    CMakeLists.txt

  SDK/retro68/          Mac-side developer kit
    PurrIPC.h           C header for Mac apps (IPC command IDs, structs, API)
    PurrIPC.c           Implementation — writes to 0xF00000, spin-polls DONE
    README.md           SDK usage guide
    apps/
      purr_lora_chat/   Example app: LoRa chat terminal in a Mac window
        main.c
        Makefile
```

---

## Setup

### 1. Get the ROM image

MagicMac requires a 512 KB Mac Plus ROM dump. This is not included for legal reasons.

The file must be named `mac.rom` and placed at:

```
CoreOS/spiffs_image/mac.rom
```

The SPIFFS image is flashed alongside the firmware. The emulator loads it at boot from `/spiffs/mac.rom`.

### 2. Vendor umac and Musashi

The emulator source is not included. Clone both into `CoreOS/components/drv_umac/`:

```sh
cd magicmac/CoreOS/components/drv_umac

# umac — Mac Plus emulator core
git clone https://github.com/evansm7/umac umac

# Musashi — 68k CPU core (umac depends on this)
git clone https://github.com/kstenerud/musashi musashi
```

Then update `CMakeLists.txt` `SRCS` to match the actual source file names in those repos.

### 3. Wire the IPC window into umac

In `umac_core.c`, after `umac_init_core()` succeeds, register the IPC buffer so the 68k memory bus traps writes to `0xF00000`:

```c
// Replace the stub comment in umac_core.c with:
umac_register_ipc_window(&s_ipc, PURR_IPC_BASE, sizeof(s_ipc));
```

The exact API depends on how evansm7/umac exposes memory-mapped I/O hooks. Check `umac/umac.h` in the cloned repo.

### 4. Add PURR_HAS_CLASSIC_MAC to your build

In `CoreOS/CMakeLists.txt`, add `purr_classic` to `EXTRA_COMPONENT_DIRS` and set the compile flag:

```cmake
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/../magicmac/Shells/purr_classic"
    "${CMAKE_SOURCE_DIR}/../magicmac/CoreOS/components/drv_umac"
    "${CMAKE_SOURCE_DIR}/../magicmac/CoreOS/components/lib_purr_ipc"
)
```

In your `sdkconfig` or `CMakeLists.txt` target compile options:

```cmake
add_compile_definitions(PURR_HAS_CLASSIC_MAC)
```

In `CoreOS/system/system/main.cpp`, add the shell selection alongside the existing shell guards:

```cpp
#ifdef PURR_HAS_CLASSIC_MAC
    purr_classic_start();   // does not return
#endif
```

### 5. Enable PSRAM (recommended)

System 3 runs in 512 KB which fits in internal SRAM on CYD. If you want System 6 compatibility or headroom, enable PSRAM in `sdkconfig`:

```
CONFIG_ESP32_SPIRAM_SUPPORT=y
CONFIG_SPIRAM_USE_MALLOC=y
```

### 6. Flash and test

```sh
cd CoreOS
idf.py -p /dev/ttyUSB0 flash monitor
```

On boot the emulator will load the ROM, initialise 512 KB of RAM, and start the 68k CPU. The Mac boot chime sound is not emitted (no audio driver yet). The happy Mac icon will appear on screen within a couple of seconds.

---

## Building Mac apps (Retro68)

See [SDK/retro68/README.md](SDK/retro68/README.md) for full instructions.

Quick start:

```sh
# Install Retro68
git clone https://github.com/autc04/Retro68
cd Retro68 && ./build-all.sh

# Build the example app
cd magicmac/SDK/retro68/apps/purr_lora_chat
make
# Produces purr_lora_chat.dsk — copy to Mac disk or load via umac disk image support
```

---

## IPC command reference

| Command ID | Direction | AH / registers | Description |
|------------|-----------|----------------|-------------|
| `0x01` | Mac to ESP32 | payload = bytes | Send LoRa packet |
| `0x02` | ESP32 to Mac | response = bytes | Receive LoRa packet |
| `0x10` | Mac to ESP32 | — | Get WiFi status |
| `0x11` | Mac to ESP32 | payload = SSID scan trigger | Scan networks (JSON response) |
| `0x12` | Mac to ESP32 | payload = ssid\0pass\0 | Connect to network |
| `0x20` | Mac to ESP32 | — | List BT paired devices (JSON) |
| `0x21` | Mac to ESP32 | payload = addr | Pair BT device |
| `0x30` | Mac to ESP32 | payload = text | Post notification |
| `0x31` | ESP32 to Mac | response = text or LoRa | Poll for pending notification |

Full struct definitions are in [CoreOS/components/lib_purr_ipc/purr_ipc.h](CoreOS/components/lib_purr_ipc/purr_ipc.h).

---

## Status

Work in progress. The IDF components compile. The following still need to be done before MagicMac is functional:

- Vendor umac and Musashi source into `drv_umac/`
- Wire `umac_register_ipc_window()` to the actual umac memory hook API
- Implement `_on_frame()` 1bpp to RGB565 conversion and display blit
- Wire `drv_touch` callback to `_on_touch()` in `purr_classic.cpp`
- Implement MZ EXE loader in `drv_umac` if needed (COM only for now)
- Full CP437 8x8 font (replace stub in `magidos_cga.cpp`)
