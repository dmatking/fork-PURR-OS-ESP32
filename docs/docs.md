# PURR OS — Project Reference

**P.U.R.R.** — Portable Unified Runtime & Radio  
**K.I.T.T.** — Kernel Interface Translation Toolkit

PURR OS is a modular embedded OS for ESP32 devices, written in C++ on ESP-IDF with the Arduino component layer. The same codebase runs on multiple hardware targets — hardware differences are declared in a `device.json` profile, not in code.

> The original MicroPython version lives in `pre-rewrite/` for reference. All active development is in `CoreOS/`.

---

## Repo Layout

```
PURR-OS-ESP32-MicroPython/
├── Builder/                  ← Build scripts — cd here to build
│   ├── build.sh              ← Main build entry point
│   └── targets/              ← Per-device sdkconfig defaults
│       ├── heltec.defaults
│       └── cyd.defaults
│
├── CoreOS/                   ← C++ ESP-IDF project
│   ├── CMakeLists.txt        ← Conditional build (TARGET_DEVICE / BUILD_MINI)
│   ├── idf_component.yml     ← Component dependency manifest
│   ├── partitions_heltec.csv ← 8MB partition layout
│   ├── partitions_cyd.csv    ← 4MB partition layout
│   ├── system/
│   │   ├── kernel/           ← KITT kernel + device profiles
│   │   │   ├── kitt.h/.cpp   ← Full public API + 20-step boot sequence
│   │   │   ├── device_config.h/.cpp  ← Parses device.json
│   │   │   ├── main.cpp      ← Arduino entry: kitt.init() → system_start()
│   │   │   ├── devices/      ← Hardware profiles (JSON)
│   │   │   └── modules/      ← Hardware drivers
│   │   ├── micropython/      ← MicroPython runtime + import kitt bridge
│   │   ├── bridge/           ← Key mapping + radio handoff
│   │   └── system/           ← Shell launcher + memory monitor
│   ├── apps/
│   │   ├── smol/             ← Text shell for 128×64 OLED (Heltec)
│   │   └── launcher/         ← Touch launcher OS (CYD)
│   └── boot/
│       ├── watchdog/         ← FreeRTOS heartbeat watchdog
│       └── emergency/        ← BOOT-pin recovery flasher
│
├── CattoHID/                 ← ESP32-S2 USB HID keyboard firmware (standalone)
│
├── LoRa Kernels/             ← Drop-in lora_manager.h/.cpp pairs
│   ├── RAK3172/              ← UART AT commands (active board target)
│   ├── SX1262/               ← SPI, Heltec V3 / T-Deck
│   └── SX1276_RFM95W/        ← SPI, generic breakout
│
├── Userland/                 ← Python .meow app bundles (porting reference)
├── PURR_OS_docs/             ← Extended specification documents
└── pre-rewrite/              ← Original MicroPython implementation (reference only)
```

---

## Target Devices

| Target | Chip | Flash | Display | Input | Shell | Status |
|---|---|---|---|---|---|---|
| **Heltec WiFi LoRa 32 V3** | ESP32-S3 | 8MB | SSD1306 128×64 OLED | 2 buttons | smol (text) | Active |
| **CYD** (ESP32-2432S028R) | ESP32 | 4MB | ILI9341 320×240 | XPT2046 touch | Launcher OS | Active |
| **LilyGo T-Deck** | ESP32-S3 | 16MB | ST7789 320×240 | Trackball + BB keyboard | BB OS 6 (WIP) | Planned |
| **CattoPad** | ESP32-S3 | 16MB + 8MB PSRAM | ILI9488 320×480 | mXT336T touch + 4×5 keypad | explorer.meow | Legacy reference |

### Device profiles (`CoreOS/system/kernel/devices/`)

Each target has a `<name>.json` that KITT reads at boot:

```jsonc
// heltec.json
{
  "device": "heltec_v3",
  "display": "ssd1306",
  "display_res": [128, 64],
  "display_pins": { "sda": 17, "scl": 18, "rst": 21 },
  "psram": false,
  "flash": "8mb",
  "pi_slot": false,
  "radios": ["wifi", "lora"],
  "lora_pins": { "sck": 9, "mosi": 10, "miso": 11, "cs": 8, "rst": 12, "busy": 13, "dio1": 14 },
  "buttons": { "boot": 0, "user": 47 },
  "keymap": { "boot": "SELECT", "user": "BACK" },
  "verbose": true
}
```

```jsonc
// cyd.json
{
  "device": "cyd",
  "display": "ili9341",
  "display_res": [320, 240],
  "touch": "xpt2046",
  "psram": false,
  "flash": "4mb",
  "radios": ["wifi", "bt"],
  "cpu_max_mhz": 240
}
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Apps (.meow Python / C++ built-in)                      │
│    smol (OLED text shell) │ launcher (CYD touch OS)      │
│    explorer.meow  │  classicmac.meow  │  purr_ui.meow    │
├─────────────────────────────────────────────────────────┤
│  system/system  — shell routing, memory monitor, OTA     │
├─────────────────────────────────────────────────────────┤
│  system/bridge  — keymap translation, radio handoff      │
├─────────────────────────────────────────────────────────┤
│  KITT Kernel    — all hardware, process table, 20-step   │
│                   boot, NVS heartbeat, LVGL indev         │
├─────────────────────────────────────────────────────────┤
│  Hardware modules (loaded based on device.json)          │
│  display_ssd1306 │ display_ili9341 │ display_ili9488      │
│  touch_xpt2046   │ touch_mxt336t                          │
│  lora_manager    │ wifi_manager    │ bt_manager           │
│  partition_manager │ power_manager │ pi_manager           │
├─────────────────────────────────────────────────────────┤
│  boot/watchdog  — FreeRTOS heartbeat, restarts KITT      │
│  boot/emergency — BOOT-pin recovery (no KITT needed)     │
└─────────────────────────────────────────────────────────┘
```

**One rule**: KITT is the only code that touches hardware. Every layer above calls KITT APIs.

---

## KITT — Key APIs

Full API in `CoreOS/system/kernel/kitt.h` (~115 methods). Common ones:

```cpp
// Display
kitt.text_print(row, text);         // SSD1306: 8 rows × 16 chars
kitt.text_clear();
kitt.display_width() / display_height()

// Input
kitt.get_key_event(&key, &pressed); // reads generic key ring buffer
// Keys: KEY_UP, KEY_DOWN, KEY_SELECT, KEY_BACK

// WiFi / LoRa
kitt.wifi_connected();  kitt.wifi_connect(ssid, pass);
kitt.lora_enabled();    kitt.lora_send(buf, len);
kitt.lora_available();  kitt.lora_read(buf, max_len);

// Process management
kitt.app_launch(path);              // launches a .meow Python app
kitt.process_running(path);
kitt.process_kill(path);

// System info
kitt.os_name();         // "PURR OS" (LoRa present) or "PUR OS"
kitt.device_name();     // from device.json "device" field
kitt.battery_percent();
kitt.free_ram_kb();
kitt.cpu_get_freq_mhz();

// Callbacks
kitt.set_memory_warning_cb(fn);   // called at 90/95/98% RAM
kitt.set_crash_report_cb(fn);
```

---

## KITT Boot Sequence (20 steps)

Runs inside `KITT::init()` called from `main.cpp`:

| Step | Action | Failure |
|---|---|---|
| 1 | Serial 115200 | — |
| 2 | Mount SPIFFS | halt |
| 3 | Parse device.json | halt |
| 4 | Apply verbose flag, device name | — |
| 5 | Display init (ssd1306 / ili9341 / ili9488) | serial fallback |
| 6 | LVGL init + flush callback | text fallback |
| 7 | Boot splash or verbose log | — |
| 8 | Check NVS flash flag → flasher mode | — |
| 9 | WiFi init (async connect if saved SSID) | warn, continue |
| 10 | BT init | warn, continue |
| 11 | LoRa init (if in radios) → sets os_name | warn, continue |
| 12 | Touch init + LVGL indev registration | warn, continue |
| 13 | Pi manager (if pi_slot) | warn, continue |
| 14 | Power manager + CPU freq | — |
| 15 | MicroPython runtime init | — |
| 16 | apps_scan() | warn, empty list |
| 17 | firmware_scan() | warn, empty list |
| 18 | Write KITT_READY to NVS | — |
| 19 | First NVS heartbeat | — |
| 20 | Spawn system_start() | text error + wait watchdog |

---

## Shell Routing (system/system/main.cpp)

After KITT init, `system_start()` picks the right shell:

```
display_width() <= 128  →  smol_start()         (Heltec OLED)
device == "cyd"         →  launcher_start()      (CYD touch launcher)
otherwise               →  app_launch(explorer.meow)  (large display)
```

---

## CYD Launcher OS

The CYD target boots a standalone firmware launcher, not a Python shell. PURR OS lives in the factory partition (permanent) and manages up to 2 OTA firmware slots via `partition_manager`.

- **SD card** (VSPI: CS=5, CLK=18, MOSI=23, MISO=19) — source for .bin/.purr installs
- **Touch** (software SPI: MOSI=32, MISO=39, SCLK=25, CS=33, IRQ=36) — XPT2046
- **Display** (HSPI: MOSI=13, SCLK=14, MISO=12, CS=15, DC=2) — ILI9341, backlight GPIO21
- **Partition layout**: factory 1.25MB + ota_0 1.375MB + ota_1 1MB + spiffs 320KB

Launching a firmware: `pm_launch(slot)` calls `esp_ota_set_boot_partition()` + `esp_restart()`. To return to PURR OS launcher, the launched firmware must call `esp_ota_set_boot_partition(factory)` + restart.

---

## LoRa Kernels

Three drop-in backends with identical APIs. Copy any folder's contents into `CoreOS/system/kernel/modules/` to switch radios:

| Folder | Radio | Interface | Use when |
|---|---|---|---|
| `LoRa Kernels/SX1262/` | SX1262 | SPI | Heltec V3, T-Deck |
| `LoRa Kernels/RAK3172/` | RAK3172 (STM32WL) | UART AT | CattoBoardV1 PCB |
| `LoRa Kernels/SX1276_RFM95W/` | SX1276/RFM95W | SPI | Generic breakouts |

All implement: `init`, `update`, `deinit`, `send`, `read`, `data_available`, `busy`, `yield`, `reclaim`, `set/get freq/power/SF/BW/CR`.

---

## CattoHID

Separate firmware in `CattoHID/` for the ESP32-S2 co-processor on CattoBoardV1. It runs a pure USB HID keyboard — 6×14 matrix scan, 3-sample debounce, 6KRO HID reports. No WiFi, no display, no UART to the CM5. Build independently with `idf.py set-target esp32s2`.

---

## MicroPython Runtime

Full builds (not `--mini`) include a MicroPython runtime embedded in KITT. Python `.meow` app bundles can `import kitt` and call:

```python
import kitt

kitt.text_print(0, "Hello")
kitt.wifi_connect("ssid", "pass")
kitt.lora_send("hello mesh")
msg = kitt.lora_read()
kitt.notify("toast message")
```

The MicroPython submodule must be cloned and built before a full build compiles. See `Builder/HOWTO.md`.

---

## Watchdog + Emergency

**`boot/watchdog/`** — FreeRTOS task that reads the KITT NVS heartbeat every 1s. If stale >3s, calls `esp_restart()`. Waits for `KITT_READY` flag before starting. Built as a separate partition (never overwritten by PURR OS OTA).

**`boot/emergency/`** — Checks BOOT pin at power-on. If held, inits minimal display + SPIFFS and flashes `/recovery/recovery.bin` via OTA. 60s timeout then reboot. Zero dependency on KITT or LVGL.

---

## Extended Specs

Full specification documents live in `PURR_OS_docs/`:

| File | Contents |
|---|---|
| `01_Architecture.md` | Layer diagram, design rules, module load conditions |
| `02_KITT_Kernel_Spec.md` | Full KITT API reference |
| `03_ControlPanel_Spec.md` | Control panel UI spec |
| `04_AppBundle_Format.md` | .meow bundle format + manifest.json |
| `05_Boot_Sequence.md` | Detailed boot sequence with code samples |
| `06_WindowsCE_UI_Spec.md` | Windows CE shell spec |
| `07_Windows95_UI_Spec.md` | Windows 95 shell spec |
| `08_MacSystem75_UI_Spec.md` | Mac System 7/8 shell spec |
| `09_CattoBoardV1_Spec.md` | CattoBoardV1 PCB — CM5 carrier, ESP32-S2 HID |
| `10_Handshake_Protocols.md` | CM5 ↔ RAK3172 UART AT protocol |
| `11_PURR_HID_Edition.md` | ESP32-S2 PURR-lite — USB HID + MSC OTA |
| `12_TDeck_BlackBerry6_UI_Spec.md` | T-Deck BB OS 6 LVGL spec |

---

## Build

See `Builder/HOWTO.md` for full step-by-step instructions.

Quick reference:
```bash
cd Builder
./build.sh --target heltec --mini            # Heltec, no MicroPython
./build.sh --target cyd                      # CYD full build
./build.sh --target heltec --mini --flash COM5  # build + flash
```
