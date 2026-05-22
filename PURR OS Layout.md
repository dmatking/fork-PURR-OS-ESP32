# PURR OS — ESP32-S3 16MB Flash Layout

## Portable Unified Runtime & Radio — Codename: CattoPad

### Kernel: KITT (Kernel Interface Translation Toolkit)

> **Status: Brainstorming / architecture draft.** This is a living design document capturing the PURR OS architecture as worked out so far. It is not final — sizes are estimates, some subsystems (kernel self-test harness, debug-kernel validation) are still under discussion and not yet specced here. Intended as a shareable reference for collaborators.

**TL;DR** — PURR OS is a modular MicroPython OS for ESP32-S3 devices. Its core is **KITT**, a slim kernel that handles hardware abstraction, radio handoff, and key translation so upstream firmware (Meshtastic, Bruce) runs stock from a `/friends/` folder with no patches. A `device.json` profile plus pluggable `/modules/` let the same KITT run from a cheap Heltec V3 up to the full CattoPad cyberdeck. The UI (explorer.app) and orchestration (system.app) are optional, swappable layers on top.

## Naming

|Name          |Stands for                          |What it is                                                                         |
|--------------|------------------------------------|-----------------------------------------------------------------------------------|
|**PURR**      |Portable Unified Runtime & Radio    |The OS                                                                             |
|**KITT**      |Kernel Interface Translation Toolkit|The kernel                                                                         |
|**bridge.app**|—                                   |Translation layer (key mapping + radio handoff) that sits between KITT and firmware|
|**CattoPad**  |—                                   |Codename for the flagship hardware (Ingenico Move 5000 cyberdeck)                  |

-----

-----

-----

## Flash Partition Layout

|Region               |Size     |Notes                                           |
|---------------------|---------|------------------------------------------------|
|Bootloader           |~256KB   |ESP-IDF bootloader, reads boot flags            |
|NVS                  |~200KB   |Settings, state, hardware config                |
|OTA staging partition|~4MB     |PSRAM-assisted staging for firmware flashing    |
|`/boot/`             |~512KB   |flasher.py, watchdog.py, boot flags             |
|`/system/`           |~4–5MB   |KITT, system.app, explorer.app bundles          |
|`/apps/`             |~1MB     |User .app bundles                               |
|`/friends/`          |~4–6MB   |Meshtastic, Bruce, third-party firmware binaries|
|Headroom             |~1–2MB   |Future apps, assets, overflow                   |
|**Total**            |**~16MB**|                                                |

-----

## Minimum Viable OS

The absolute floor — device is stable and usable with only these present:

```
/boot/watchdog.py
/system/kernel.app  (KITT core + the modules that device needs)
```

KITT provides display, keypad/button abstraction, WiFi, BT, basic text UI, basic process execution, and firmware detection. It can spawn and run executables (including a `/friends/` firmware like Meshtastic) on its own. Everything above this — system.app, explorer.app, OTA, the full shell — is optional layering.

This is also the **prototyping path**: bring up KITT + watchdog on cheap hardware (Heltec V3 / V4 or a bare ESP32-S3), validate the fundamentals — module loading, radio handoff, key abstraction, firmware drop-in — then stack system.app + explorer once CattoPad hardware arrives. Same KITT underneath the whole way.

-----

## Boot Order

```
1. watchdog.py          ← loads first, spawns + monitors KITT
2. KITT           ← hardware init, WiFi, BT, display, keypad, text UI
   │
   ├── boot flag set? ──► FLASHER MODE:
   │                       KITT loads basic text UI, keypad only
   │                       writes staged image → clears flag → reboots
   │
   └── normal boot:
3. KITT reads friends.txt + scans /friends/ → registers firmwares
4. KITT spawns system.app
5. system.app loads LoRa module (if present)
6. system.app loads explorer.app
```

On `system.app` restart:

- KITT holds display, keypad, and all running process state
- watchdog.py signals KITT to restart system.app
- system.app comes back up, re-handshakes with KITT
- No app dropouts, no rescan needed

-----

## Folder Structure

```
/
├── boot/
│   ├── watchdog.py         # Loads first — spawns + monitors KITT, owns restart authority
│   └── emergency.py        # Standalone recovery flasher + MTP — KITT-independent safety net
│
├── system/
│   ├── kernel.app/         # Slim core loader — reads device.json, imports only needed modules
│   │   ├── main.py         # Core loader
│   │   ├── device.json     # Hardware profile — declares present hardware
│   │   ├── modules/        # Per-hardware driver modules, loaded conditionally
│   │   │   ├── display.py  # ILI9488 / SSD1306 / etc — selected by device.json
│   │   │   ├── touch.py    # mXT336T driver
│   │   │   ├── wifi.py     # WiFi management
│   │   │   ├── bt.py       # Bluetooth management
│   │   │   ├── lora.py     # LoRa (pluggable)
│   │   │   ├── pi.py       # Pi gate / handshake / power management
│   │   │   ├── mtp.py      # MTP USB mode
│   │   │   ├── flasher.py  # Kernel flasher mode
│   │   │   └── power.py    # Battery, CPU freq, rail management
│   │   └── assets/
│   ├── system.app/         # App lifecycle, memory monitor, OTA handoff
│   │   ├── main.py
│   │   └── assets/
│   ├── bridge.app/         # Translation layer — key mapping + radio handoff brokering (kept out of KITT)
│   │   ├── main.py
│   │   └── assets/
│   └── explorer.app/       # Swappable UI shell — Windows CE default
│       ├── main.py
│       └── assets/
│           └── icons/      # Windows 95/CE icon assets
│
├── apps/                   # User .app bundles (auto-scanned, auto-added to Start menu)
│   ├── controlpanel.app/
│   ├── ota.app/
│   ├── notes.app/
│   ├── keyboard.app/       # On-screen touch keyboard — overlay input method
│   └── msn.app/
│
├── friends/                # Third-party firmware binaries (auto-scanned by KITT)
│   ├── friends.txt         # Manifest — metadata for known firmwares
│   ├── meshtastic.bin
│   └── bruce.bin
│
└── update/                 # OTA drop folder (monitored by ota.app)
    └── (firmware.bin dropped here by user or OTA pull)
```

-----

## App Inventory

|App               |Bundle              |Approx Size                           |Notes                                                                                   |
|------------------|--------------------|--------------------------------------|----------------------------------------------------------------------------------------|
|MicroPython core  |—                   |~1.5MB                                |lv_micropython + LVGL                                                                   |
|Watchdog          |`/boot/watchdog.py` |~50KB                                 |Loads first, owns restart authority                                                     |
|KITT kernel       |`kernel.app`        |~800KB–1MB (core ~100–200KB + modules)|Foundation: HW abstraction, radios, key + radio translation, basic process exec, text UI|
|LoRa module       |pluggable           |~200–300KB                            |Optional — KITT loads if present                                                        |
|System daemon     |`system.app`        |~800KB–1MB                            |App lifecycle, memory monitor, OTA handoff                                              |
|Bridge            |`bridge.app`        |~150–300KB                            |Key mapping + radio handoff brokering — keeps translation logic out of KITT             |
|Explorer          |`explorer.app`      |~1.5–2MB                              |Swappable UI shell                                                                      |
|Control Panel     |`controlpanel.app`  |~500KB                                |Settings UI                                                                             |
|OTA Updater       |`ota.app`           |~500KB                                |Firmware update flow                                                                    |
|Notes             |`notes.app`         |~200–300KB                            |Lightweight text notes                                                                  |
|Touch Keyboard    |`keyboard.app`      |~150–250KB                            |On-screen keyboard, overlay input method                                                |
|MSN Client        |`msn.app`           |~400–600KB                            |Meshtastic chat, MSN 4.0 aesthetic                                                      |
|Flasher           |KITT mode           |—                                     |Normal flash flow — KITT boot-flag branch, text UI + keypad                             |
|Emergency Recovery|`/boot/emergency.py`|~200KB                                |Standalone recovery flasher + MTP, KITT-independent                                     |
|Meshtastic        |`/friends/`         |~2–4MB                                |Auto-detected                                                                           |
|Bruce             |`/friends/`         |~1.5–1.8MB                            |Auto-detected, WiFi + BT modules                                                        |
|**Total**         |                    |**~10–14MB**                          |~2–6MB headroom                                                                         |

-----

## watchdog.py — Write-up

Loads first, before MicroPython’s main stack settles. Owns restart authority for the whole OS.

- Spawns and monitors KITT
- Polls KITT and system.app health
- Restarts either if unresponsive or over memory threshold
- Validates a new/swapped KITT bundle before committing to it (basic integrity); falls back to last known-good KITT if the new one fails
- Stays under 50KB — never touches the main process heap
- Survives upper-layer crashes; the last line of defence before a hard reset

-----

## KITT Kernel — Write-up

Foundation layer. Minimum viable OS when paired with watchdog.py. Owns all hardware, never goes down with the UI.

### Modular architecture

KITT is a slim core loader (~100–200KB). On boot it reads `device.json`, then imports only the driver modules the hardware actually has from `kernel.app/modules/`. Each module is a separate script — absent hardware means the module is never imported, keeping the footprint minimal on constrained devices.

```json
{
  "device": "cattopad",
  "display": "ili9488",
  "display_res": [320, 480],
  "touch": "mxt336t",
  "psram": true,
  "flash": "16mb",
  "pi_slot": true,
  "radios": ["wifi", "bt", "lora"]
}
```

Heltec V3 example: loads `wifi.py`, `bt.py`, `lora.py`, `display.py` (SSD1306) only — skips touch, Pi, MTP, PSRAM features. CattoPad loads everything. One codebase, many targets.

### Responsibilities

- **Display ownership** — survives system.app and explorer.app crashes
- **Keypad ownership** and input routing — always responsive
- **Basic text UI fallback** — rendered directly if explorer.app is absent or crashed
- **WiFi** control + status API (native ESP32)
- **Bluetooth** control + status API (native ESP32)
- **LoRa** — pluggable module, loaded if present, skipped if absent
- **Button / key abstraction** — reads physical pins, maps them via a per-device keymap to generic keycodes, so firmware sees device-independent input (see Key Abstraction Layer)
- CPU frequency scaling (`machine.freq()`)
- MTP USB mode negotiation + switching
- Basic process execution — KITT can spawn and manage executables on its own. system.app is **not required**; a device runs with just watchdog + KITT (+ device modules). system.app only adds higher-level orchestration on top.
- **Flasher mode** — on boot, if the boot flag is set, KITT enters flasher mode instead of normal boot: basic text UI, keypad only, writes staged image, clears flag, reboots
- **Firmware detection:**
  - Scans `/friends/` for `.bin` and `.fw` files automatically
  - Cross-references `friends.txt` for metadata enrichment
  - Auto-registers any binary found, even without a manifest entry
  - Exposes firmware list to system.app and explorer.app
- Manages Meshtastic and Bruce process lifecycle
- **Radio handoff** — built-in protocol lets `/friends/` firmware request ownership of WiFi/BT (and optionally LoRa); KITT yields so firmware runs upstream-unmodified, reclaims on exit (see Radio Handoff Protocol)
- Status refresh cycle: battery every 60s; BT / WiFi / LoRa staggered every 15s
- Hot-reloadable via `importlib.reload()` without full reboot

### friends.txt format

```
[meshtastic]
name = Meshtastic
file = meshtastic.bin
type = firmware
args = --headless

[bruce]
name = Bruce
file = bruce.bin
type = firmware
args = --wifi --bt
```

Presence in the manifest enriches an entry; absence does not block detection.

-----

## system.app — Write-up

Optional orchestration layer, sits above KITT. KITT can run basic apps without it; system.app adds higher-level process management. The design deliberately splits responsibilities: **KITT keeps hardware translation, hardware management, and basic process execution; system.app takes lifecycle orchestration, mutual exclusivity, and memory monitoring.** This keeps KITT lean and portable to constrained devices.

- App process tracking and lifecycle (advanced)
- Mutual exclusivity enforcement (Meshtastic vs Bruce)
- Auto-start app management
- OTA handoff: receives staged file from ota.app, sets boot flag, triggers reboot
- Explorer watchdog: relaunches explorer.app if crashed, passes crash report on relaunch
- **Memory monitor:**
  - 90%: warning overlay via explorer.app
  - 95%: urgent dialog — prompts user to close an app
  - 98%: auto-kills heaviest non-critical process, notifies explorer.app
  - Self-pressure: saves state to NVS, signals watchdog to restart via KITT
- Calls KITT for all hardware state

-----

## bridge.app — Write-up

Translation layer between KITT (pure hardware) and firmware/apps. Exists to keep all “smart” translation logic out of KITT so the kernel stays a dumb, stable HAL. Swappable and updatable without touching the kernel.

### Responsibilities

- **Key mapping** — owns the per-device keymap (GPIO → generic keycode). KITT emits raw hardware events; bridge.app maps them. If the keymap is missing or malformed, KITT falls back to raw GPIO values so nothing hard-breaks.
- **Radio handoff brokering** — coordinates the handoff protocol between KITT’s radio primitives and a `/friends/` firmware requesting exclusivity. KITT exposes yield/reclaim; bridge.app decides when and to whom.
- **Drag-and-drop firmware support** — this is what makes a `/friends/` binary work unmodified: bridge.app feeds it generic keycodes and brokers its radios, so the upstream firmware sees its native environment.

### Design rule

KITT stays pure HAL: read pin → return value, yield radio → reclaim radio, no business logic. The moment translation/decision logic would land in KITT, it goes in bridge.app instead. This is the single biggest lever for kernel stability — minimal surface area, nothing to break.

-----

-----

## explorer.app — Write-up

The primary UI shell. Fully swappable — owns no hardware, calls KITT for all data.

### Responsibilities

- **Taskbar** (bottom, 320×32px, persistent):
  - Start button (left)
  - Running app buttons (center, max 3 slots)
  - System tray (right): battery, BT, WiFi, LoRa, clock
- **Status indicators** — explorer renders battery, Bluetooth, WiFi (and LoRa) state in the tray. Values come from KITT via `update_tray(state)`; explorer owns the visual representation and refresh of the icons
- **Applets** — small tray-resident widgets (e.g. battery detail popover, WiFi picker, BT device list) that expand from the system tray on tap
- **Notifications** — toast/notification surface for system and app events; queued and dismissable, distinct from blocking popups
- **Start menu** (Windows 95 style modal):
  - Asks KITT for available apps (`/apps/` scan)
  - Asks KITT for available friends (`/friends/` + friends.txt)
  - Populates menu with both; shows install prompt for missing firmware
- **File manager / file picker** (Windows CE style):
  - Browsable file explorer across OS folders
  - Reusable file picker surface (used by OTA, notes, app loaders, etc.)
- **All popups** — memory warnings, kill notifications, mutual exclusivity alerts, crash reports
- App launch requests are passed to system.app, which brokers via KITT

### Discovery flow

```
explorer.app startup
   ↓
calls KITT.list_apps()       → /apps/ bundles with valid main.py
calls KITT.list_friends()    → /friends/ binaries + friends.txt metadata
   ↓
builds Start menu entries (installed apps + detected firmwares)
   ↓
missing-but-expected firmware  → shows "Install" prompt instead of launch
```

### Requirements for a replacement UI

Any swap-in `explorer.app` must:

- Implement the contract calls below
- Query KITT for app and friends lists rather than scanning directly
- Render battery, BT, and WiFi status from KITT-supplied values
- Provide a file picker/explorer surface for other apps to call
- Surface applets and a notification queue
- Route all launch requests through system.app
- Render its own popup surface for system/KITT notifications
- Handle a graceful teardown when system.app signals a restart
- Scale to the target display size (or fall back to text)

|Contract call            |Description                                       |
|-------------------------|--------------------------------------------------|
|`ready()`                |Signal to system.app that UI is loaded            |
|`show_popup(msg)`        |Display a blocking warning/notification dialog    |
|`notify(event)`          |Push a dismissable toast notification             |
|`show_crash_report(data)`|Display kill/crash report from system.app         |
|`restart_handler()`      |Graceful UI teardown on system restart            |
|`update_tray(state)`     |Receive battery/BT/WiFi/LoRa/clock state from KITT|
|`open_picker(opts)`      |Expose file picker, return selected path          |
|`list_refresh()`         |Re-query KITT for apps/friends (hot-plug)         |

Replacement can be: full LVGL Windows CE UI (default), a minimal stub for low-memory targets, a custom UI for other hardware, or a PIN pad / payment terminal UI.

If explorer.app is missing or fails the contract, KITT renders its built-in text UI directly.

-----

## UI Portability & Per-Device Strategy

explorer.app is one UI target, not the only one. Because explorer is contract-based and swappable, each device can ship the UI that fits its hardware.

### Per-device options

|Device        |Display              |Recommended UI                                                                        |
|--------------|---------------------|--------------------------------------------------------------------------------------|
|CattoPad      |320×480 + touch      |Full Windows CE explorer.app                                                          |
|Heltec V3     |128×64 OLED + buttons|Stock upstream Meshtastic UI (board designed for it) **or** compact list-launcher stub|
|Future targets|per device.json      |Layout profile                                                                        |

For the V3 specifically, running stock Meshtastic is the easiest and best-fit call — the board was designed for that firmware and the 128×64 screen is too small to justify a custom shell. The KITT + friends + radio-handoff model already boots it unmodified.

### Optional: portable UI abstraction layer

If a consistent custom UI across devices is wanted (rather than stock Meshtastic on small targets), the cleaner path is a UI abstraction layer rather than per-device shells:

- Apps (msn, notes, control panel) call abstract primitives — `draw_list()`, `draw_menu()`, `show_dialog()`
- A render layer picks the layout for the target display declared in `device.json`
- LVGL supports both ILI9488 and SSD1306, so the same LVGL calls retarget with different layouts/font sizes

```
320×480 + touch  → full Windows CE explorer
128×64 OLED      → compact scrolling list launcher
new device       → new layout profile
```

One app codebase, swap the layout engine per device. Tradeoff: abstraction costs flash and complexity, and OLED vs 320×480 layouts differ enough that each profile still needs hand-tuning — “write once” only goes so far.

-----

Settings frontend. Holds no state — calls KITT and system APIs.

- WiFi control (calls KITT)
- Bluetooth control (calls KITT)
- Personalization: wallpaper, color scheme (basic)
- Memory monitor view (reads system.app stats)
- Hardware test mode
- Launches OTA flow (hands off to ota.app)

-----

## ota.app — Write-up

Firmware update flow, surfaced to the user.

- Monitors `/update/` drop folder for new firmware
- Checks online for updates (when WiFi available)
- Prompts user on detection: Update now / Later / Dismiss
- On confirm: moves + renames file to `/update/update_firmware.bin`
- Notifies system.app → boot flag set → reboot → flasher runs
- Dismissable — user can defer and reboot later

-----

## notes.app — Write-up

Lightweight text notes. The reference “small app” — proves the lightweight-app model.

- Plain text editing
- Saves to local storage
- Runs alongside other lightweight apps (not demanding apps)
- Minimal footprint, no KITT hardware calls beyond keypad input

-----

## keyboard.app — Write-up

On-screen touch keyboard. An overlay input method, not a fullscreen app — summoned on top of whatever app needs text entry.

- Touch-driven QWERTY layout, scaled to display width (320px portrait)
- Summoned by any app requesting text input; dismisses on submit/cancel
- Returns entered text to the calling app via callback
- Renders above the active app, below blocking popups
- Touch only — on ESP32-only mode (no touch), apps fall back to keypad entry
- Lightweight; coexists with the active app rather than counting as a taskbar slot
- Swappable like any `.app` — a replacement just needs to honor the input-request/return contract

> In Pi mode the mXT336T touch controller drives this. In ESP32-only mode where touch may be absent, explorer routes text entry to the physical keypad instead.

-----

Meshtastic chat client with MSN Messenger 4.0 aesthetic.

- Calls Meshtastic API (headless backend, no embedded UI)
- Contact list + chat windows + status indicator
- Classic MSN beveled buttons, color palette
- Can minimize to taskbar (lightweight app)
- Talks to Meshtastic over the KITT’s managed process

-----

## Flasher — Write-up

Two paths: the normal in-KITT flasher, and a standalone emergency recovery.

### Normal flash (KITT mode)

Handled by KITT, not a separate stub. On boot, KITT checks the boot flag:

- If set, KITT enters **flasher mode** instead of normal boot
- Loads basic text UI, accepts keypad input only (no explorer, no touch dependency)
- Writes the staged image from `/update/`
- Clears the boot flag, reboots into normal OS
- Reuses KITT’s own display/keypad/text UI — no duplicated drivers

### emergency.py — recovery + MTP

Standalone script in `/boot/`, fully KITT-independent. The last-resort safety net for when KITT itself is broken or unflashable.

- Triggered by hardware key combo on boot (before KITT loads) or a KITT-fail fallback
- Brings up minimal display + keypad directly, text mode only
- Exposes the device over **MTP** so a PC can drop a fresh image
- Can flash a recovery image without any of the normal stack present
- No dependency on KITT/system/explorer — survives a bricked KITT

> Tradeoff captured: normal flashing rides on KITT (lean, reuses its UI), but KITT must be intact. emergency.py covers the case where it isn’t — including reflashing the KITT or MicroPython base itself.

-----

## Radio Handoff Protocol

Built into KITT by default. Lets any `/friends/` firmware (Meshtastic, Bruce) take ownership of radios it needs and run **upstream-unmodified** — no patches, no forks. This preserves the integrity of the upstream projects.

### Why

Meshtastic and Bruce are compiled binaries that expect to own BLE/WiFi natively (including Meshtastic’s BLE encryption and pairing, which it handles end-to-end). They can’t run under MicroPython directly. So KITT runs them as headless `/friends/` binaries and yields the radios — the same handoff pattern used for the Pi display, applied to radios instead.

### Handoff flow

```
firmware launched (e.g. meshtastic.bin)
        ↓
firmware signals radio request (WiFi / BT / both) over KITT IPC
        ↓
KITT quiesces its own radio use → yields requested radios
        ↓
firmware owns radios — handles its own stack + encryption upstream-unmodified
        ↓
firmware killed → KITT reclaims radios, resumes normal use
```

### Ownership while active

- **Meshtastic active** → exclusive BLE + WiFi + LoRa; iPhone pairs directly to Meshtastic’s native BLE (encrypted end-to-end, KITT never sees plaintext or ciphertext)
- **Bruce active** → exclusive WiFi + BT for pentesting
- **LoRa** is handed to Meshtastic for full radio exclusivity; otherwise stays KITT-managed unless a firmware explicitly requests it
- KITT retains display, keypad/buttons, touch, battery, and the reserved key combo throughout

### UI bridge (MicroPython side)

The OS never runs the BLE/encryption stack itself. msn.app consumes decrypted message objects from the running Meshtastic binary over local IPC (UART):

```
iPhone ↔ Meshtastic BLE (encrypted, native, upstream)
              ↓
         meshtastic.bin owns BLE + WiFi (KITT yielded)
              ↓
         KITT brokers process + IPC channel
              ↓
         msn.app reads decrypted messages over UART → renders chat UI
```

### Heltec V3 note

Same protocol applies. The V3 runs Meshtastic natively, so KITT yields its radios identically — only the UI layer differs (128×64 OLED stub vs 320×480 explorer).

-----

## Key Abstraction Layer

Translates physical input (a keypad on CattoPad, two buttons on a Heltec V3) into a single generic keycode stream, so firmware and apps never see hardware-specific input. KITT emits raw hardware events; **bridge.app owns the mapping** (kept out of the kernel to keep KITT a pure HAL).

### How it works

- A per-device keymap (declared in `device.json` / a keymap file) maps physical GPIO/button → generic keycode
- KITT reads the physical pins and emits raw events; bridge.app looks up the mapping and emits generic codes
- Generic codes are fed to the active app, or to a `/friends/` firmware over UART during a radio handoff
- Firmware (e.g. Meshtastic) runs upstream-unmodified — it receives generic events as if on its native hardware
- If the keymap is missing or malformed, KITT falls back to raw GPIO values — nothing hard-breaks

### Reserved combo

KITT always retains one reserved key combo (e.g. power + select) even while a firmware holds exclusivity. This guarantees the user can force-kill the firmware or return to the shell without the firmware intercepting the input. Everything else routes to the active firmware.

### Effort

Lightweight — a keymap table plus a pin-reader/lookup loop. Swapping device button layouts is a keymap change, no code change. Debug visibility (handoff events, key events, radio state transitions) can be emitted over a debug UART.

-----

## Boot Diagnostics & Verbose Logging

A `VERBOSE` flag in `device.json` toggles KITT’s diagnostic behaviour at boot. watchdog reads it and passes it to KITT. Minimal code overhead (~50–100KB); the log buffer is capped (circular) so it stays bounded.

### Verbose ON

- KITT logs everything — module loads, GPIO reads, radio handoffs, process spawns — to **display + serial** simultaneously
- **Flag breakpoints** — if KITT hits a watched flag (e.g. a module load failure), it pauses the boot sequence and waits for a key press before continuing, acting as a breakpoint
- On a **small screen**, display is too cramped — KITT logs to **serial only** and writes to a small capped ringbuffer; on a watched flag it jumps straight to MTP recovery instead of pausing

### Verbose OFF (clean boot)

- No log dump — KITT renders a **boot splash** instead
- Splash is **ASCII art** from a text file (per device, declared in `device.json`) — near-zero footprint, no image asset
- Scales to screen: large grid (~40×30) on CattoPad, tiny (~16×8) on a V3

### Error codes

- On failure, KITT emits a short error code so a user without a PC can still diagnose
- **Small screens**: shortened hex code only — e.g. `ERR:02` / `0x02` — single line, fits in 16 columns; user notes it and looks it up later
- Larger screens can show the longer symbolic form (e.g. `E_LORA_INIT_FAIL`)

### On-device log retrieval

- A reserved key combo on boot triggers a dump: KITT writes the log to `/logs/boot.txt`, then continues normal boot or enters MTP mode
- User connects over WiFi/BT/MTP and pulls the log via explorer or a simple file server
- **Emergency escalation** — if KITT can’t reach WiFi/BT/MTP, and Meshtastic is installed in `/friends/`, KITT can dump the log and hand off to Meshtastic to transmit it over **LoRa** as last-resort telemetry — automatic, no user intervention

-----

## Testing & Emulation

The OS is MicroPython, so most logic can be tested offline without ESP32 hardware.

### Recommended: local MicroPython Unix port + mock hardware

- Build the MicroPython **Unix port** locally from source (`make -C ports/unix`) — full control over heap size and features, unlike the online playground (which caps at ~256KB heap and is toy-level)
- Add a `mock_hardware.py` that stubs GPIO, display, and radios with fake implementations
- KITT and app code stay unchanged — they import the mock layer instead of real drivers under test
- Offline, lightweight, fast iteration for app/KITT logic

### ESP-IDF / QEMU

- ESP-IDF ships a unit-test framework; QEMU can emulate ESP32-S3
- Heavier setup; the MicroPython layer still needs the mock-hardware approach for KITT modules

### Wokwi

- Online emulator with ESP32-S3 + Heltec V3 templates — convenient but can be demanding on low-end hosts; the local Unix port is the offline alternative

### Hardware-only cases

Radio handoff timing, watchdog restarts, and real SPI bus contention can’t be fully captured in emulation — these need real hardware (a Heltec V3 is a cheap stand-in for the fundamentals).

-----

## OTA Flow

```
User drops firmware → /update/firmware.bin
        ↓
ota.app detects file → prompts user (Update now / Later / Dismiss)
        ↓
User confirms → ota.app moves + renames to /update/update_firmware.bin
        ↓
ota.app notifies system.app → system.app sets boot flag
        ↓
System reboots → KITT reads boot flag → enters FLASHER MODE
        ↓
KITT text UI (keypad only) → writes image → clears flag → reboots
        ↓
Normal boot → watchdog.py → KITT → system.app → explorer.app
```

-----

## Memory Management Thresholds

|Threshold               |Action                                                         |
|------------------------|---------------------------------------------------------------|
|90%                     |explorer.app shows warning overlay with per-app memory usage   |
|95%                     |Urgent dialog — user prompted to close an app                  |
|98%                     |Auto-kill heaviest non-critical process, explorer.app notified |
|system.app self-pressure|Saves state to NVS, watchdog signals KITT to restart system.app|

-----

## Mutual Exclusivity Rules

|App Type                        |Can run alongside                    |
|--------------------------------|-------------------------------------|
|Notes, MSN client, Control Panel|Any combination (max 3 taskbar slots)|
|Meshtastic (fullscreen)         |Lightweight apps only, not Bruce     |
|Bruce (fullscreen)              |Lightweight apps only, not Meshtastic|

Attempting to launch a second demanding app triggers explorer.app dialog:

> “Meshtastic is already running. Close it before launching Bruce?”

-----

## Display Arbitration (ESP32 ↔ Pi)

- Display SPI owned by KITT by default
- Pi pulls handshake GPIO high on boot → KITT releases display
- Pi takes over SPI — no hardware mux needed
- Software arbitration only
- Display context always held by KITT — survives all upper-layer restarts

-----

## Touch Driver (mXT336T) — Write-up

KITT owns the touch driver. Loaded conditionally on Pi state — touch runs in ESP32-only mode and is yielded to the Pi when present (see Touch ownership rule in Pi Power Management).

### Driver responsibilities

- **I2C init** — set address, read the object table to locate touch data in the register map
- **Object table walk** — mXT336T stores config as a tree of typed objects at dynamic addresses; driver walks the table on init to find the touch reporting object (T9 single / T100 multi-touch)
- **CHG interrupt** — chip pulls the CHG pin low when touch data is ready; driver waits on that GPIO rather than polling I2C continuously
- **Message parse** — read message processor registers, extract contact ID, X/Y, event type (press / move / release)
- **LVGL bridge** — map parsed X/Y + state into LVGL’s `indev` input interface

### Implementation notes

- Reference: Linux `drivers/input/touchscreen/atmel_mxt_ts.c` — port init + message parsing logic
- The object table walk is the main effort; raw I2C reads are straightforward
- Driver only active in ESP32-only mode — when Pi is present it stays unloaded so it never contends with the Pi for the I2C bus
- In ESP32-only mode, touch supplements the physical keypad; keypad remains the primary input for gloved/outdoor use

-----

## Pi Power Management & Auto-Detection

### Hardware setup

- ESP32 KITT gates the Pi’s power rail via a load switch (high-side P-MOSFET or dedicated load-switch IC such as TPS22xxx)
- Load switch must have inrush/slew control — CM4 can spike several amps at boot, uncontrolled inrush can brown out the shared battery rail and reset the ESP32
- Handshake GPIO: Pi pulls high when active, ESP32 holds low via pull-down when Pi is absent
- UART link (when docked): KITT can signal Pi to halt cleanly before cutting the rail
- Do not tap ADC2 pins for Pi voltage sensing — ADC2 is unusable when WiFi is active; use ADC1

### Pi state matrix (read at KITT boot, before anything else loads)

|Handshake|Gate|State                       |Kernel action                                                   |
|---------|----|----------------------------|----------------------------------------------------------------|
|LOW      |LOW |Pi absent                   |ESP32-only mode — KITT owns display, touch, WiFi, BT, LoRa      |
|LOW      |HIGH|Pi powering up              |Kernel holds everything, polls handshake, waits                 |
|HIGH     |HIGH|Pi already running          |Skip handoff sequence, release display + yield touch immediately|
|HIGH     |LOW |Anomaly (Pi up without gate)|Flag, release display, yield touch, retain radios               |

### Touch ownership rule

Touch loads in ESP32-only mode and is yielded to the Pi when it takes over:

```
Pi absent  → KITT loads mXT336T driver, routes input to LVGL
Pi present → KITT yields touch — Pi drives mXT336T over I2C with its own Linux driver
```

Touch is handed off alongside the display in the same sequence — gate high → handshake high → KITT releases display SPI and yields the I2C bus. See the Touch Driver write-up for driver detail.

### Radio ownership rule

WiFi, BT, and LoRa always remain under KITT regardless of Pi state. Other firmwares (Bruce, Meshtastic) depend on them directly. Pi gets display and compute — KITT keeps the radios.

### Power-on handoff flow

```
KITT asserts gate GPIO → Pi rail enables
        ↓
KITT polls handshake GPIO — waits for Pi to pull high
        ↓
Pi pulls high → KITT releases display SPI
        ↓
Pi takes over display — KITT retains WiFi, BT, LoRa
```

### Clean shutdown flow

```
KITT signals Pi to halt (UART)
        ↓
KITT waits for Pi to pull handshake low (shutdown confirmation)
        ↓
KITT de-asserts gate GPIO → Pi rail cut
        ↓
KITT reclaims display SPI — back to ESP32-only mode
```

### What KITT skips based on Pi state

If handshake is HIGH at boot (Pi was already running before ESP32):

- Skip display init handoff sequence
- Skip loading the touch driver — Pi owns the mXT336T directly
- Skip gating the Pi rail (already up)
- Skip loading anything that would contend for display or SPI
- Go straight to releasing display to Pi
- Retain full radio stack (WiFi, BT, LoRa always loaded)

-----

## Target Hardware

|Device                |MCU                   |Flash|PSRAM|Display                             |Input                     |Role                        |
|----------------------|----------------------|-----|-----|------------------------------------|--------------------------|----------------------------|
|CattoPad              |ESP32-S3-WROOM-1-N16R8|16MB |8MB  |Tianma TM035PDHG03 320×480 (ILI9488)|4×5 keypad + mXT336T touch|Full PURR OS target         |
|Heltec WiFi LoRa 32 V3|ESP32-S3FN8           |8MB  |none |0.96” OLED 128×64 (SSD1306)         |2 buttons                 |Prototyping / minimal target|
|Heltec V4             |ESP32-S3              |8MB  |none |OLED                                |buttons                   |Prototyping / minimal target|

Heltec V3/V4 share the ESP32-S3 core, so KITT runs unmodified — they just need a `device.json` declaring SSD1306, no touch, no Pi, and a 2-button keymap. Wokwi has a built-in Heltec WiFi LoRa 32 V3 template for emulation; CattoPad uses the generic ESP32-S3 template with the ILI9488 wired manually.

### V3 flash budget (8MB)

- Bootloader + NVS: ~0.5MB
- KITT core + watchdog + SSD1306 module: ~0.6MB
- Meshtastic (full feature set + device UI): ~2–2.5MB
- Remaining: ~4–4.5MB for utilities / stubs

On the V3, running **stock upstream Meshtastic UI** is the recommended path — the board was designed for it and the 128×64 panel doesn’t justify a custom shell. PURR’s value (full explorer, touch, Pi handoff) is realized on CattoPad.

-----

## Modularity Assessment

The core principle: **KITT is the foundation — it handles firmware, hardware management, and the translation layers (radio handoff + key abstraction). Everything else plugs in via standardized contracts.**

### Pros

- Upstream firmware (Meshtastic, Bruce) runs stock — drag-and-drop into `/friends/`, no patches, no recompile
- Device-agnostic — one KITT codebase, per-device behaviour driven by `device.json` + modules
- Clean isolation — radios and keys hand off without contention; firmware gets exclusivity
- Hot-swappable UI (explorer.app) and debug kernel via bundle drops
- Graceful degradation — minimum viable OS is just watchdog + KITT; UI failures fall back to KITT text mode
- Same kernel from prototype (V3) to product (CattoPad)

### Cons / risks

- KITT centralizes authority — a KITT bug can take the whole stack down (watchdog mitigates, but it’s the single point)
- More moving parts than a monolith — more potential failure modes in the translation layers
- Debugging a broken handoff or keymap is harder than debugging monolithic firmware
- Abstraction costs flash even on tiny devices; on an 8MB V3 every KB counts

### Mitigations in design

- watchdog validates a new KITT bundle before committing, can roll back to known-good
- Modular `/modules/` keeps the per-device footprint minimal (load only what the hardware has)
- Reserved key combo guarantees user escape from any firmware that holds exclusivity

-----

|Component|Part                                                                        |
|---------|----------------------------------------------------------------------------|
|MCU      |ESP32-S3-WROOM-1-N16R8                                                      |
|Display  |Tianma TM035PDHG03 (320×480, ILI9488)                                       |
|Touch    |Atmel mXT336T (KITT driver ESP32-only mode; Pi drives directly when present)|
|LoRa     |TBD (pluggable KITT module, shared SPI via mux IC)                          |
|Battery  |3.6V 2900mAh 3-pin JST (Ingenico OEM)                                       |