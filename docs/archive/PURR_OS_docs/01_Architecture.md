# PURR OS — Architecture Overview
**v0.9.0 / KITT v0.5.0**

---

## What is PURR OS?

P.U.R.R. = Portable Unified Runtime & Radio Operating System.

An embedded OS for ESP32 devices. It boots a modular C++ kernel (KITT), manages hardware modules (display, touch, WiFi, BT, LoRa), optionally runs a MicroPython runtime for `.meow` apps, and renders a windowed UI through the **MiniWin** window manager.

---

## Stack Layers

```
┌─────────────────────────────────────────────────────────┐
│  .meow apps  (MicroPython — optional, BUILD_MINI=0)     │
├─────────────────────────────────────────────────────────┤
│  UI Shells                                              │
│    purr_explorer    — Windows CE/PDA  (MiniWin, CYD)   │
│    purr_blackberry  — BB-style chrome (MiniWin, CYD)   │
│    smol             — OLED text shell (Heltec/T-Deck)  │
├─────────────────────────────────────────────────────────┤
│  MiniWin Window Manager  (cyd/cyd_s024c only)           │
│    PURR_CYD HAL → display_ili9341 + touch_cst816s       │
├─────────────────────────────────────────────────────────┤
│  KITT Kernel (C++)                                      │
│    wifi  bt  lora  power  partition_manager             │
│    purr_bootloader (factory image only)                 │
├─────────────────────────────────────────────────────────┤
│  ESP-IDF 5.3.5  +  FreeRTOS  (pure IDF — no Arduino)   │
└─────────────────────────────────────────────────────────┘
```

> **v0.9.0:** The Arduino dependency (`lib_arduino`) has been removed entirely. All drivers
> use pure ESP-IDF APIs. TFT_eSPI is self-contained with a minimal shim inside `lib_tftespi/`.

---

## Supported Targets

| Target | Chip | Display | Touch | Notes |
|--------|------|---------|-------|-------|
| `heltec` | ESP32 | SSD1306 128×64 OLED | — | WiFi + LoRa, Smol shell |
| `cyd_s024c` | ESP32-2432S024C | ILI9341 2.4" 320×240 | CST816S cap | Full OS → ota_0 |
| `cyd_boot` | ESP32-2432S024C | ILI9341 2.4" 320×240 | CST816S cap | Factory recovery → factory partition |
| `cyd_s028r` | ESP32-2432S028R | ILI9341 2.8" 320×240 | XPT2046 resistive | Full OS → ota_0 |
| `tdeck` | ESP32-S3 | ST7789 (WIP) | trackball | WiFi + LoRa |
| `jc3248w535` | ESP32-S3 | ST7796 (WIP) | GT911 | WIP target |
| `waveshare169` | ESP32-S3 | ST7789 (WIP) | CST816S | WIP target |

---

## CYD Partition Layout

```
nvs        20 KB    0x9000   — NVS key-value store (credentials, settings, crash counter)
otadata     8 KB    0xe000   — OTA boot selection register
factory     1 MB    0x10000  — cyd_boot: full recovery environment (never touched by OTA)
ota_0       1.5 MB  0x110000 — cyd_s024c: full PURR OS (OTA-updatable)
ota_1       1 MB    0x290000 — generic slot (third-party firmware / staging)
spiffs    448 KB    0x390000 — filesystem: device.json, app data, crash logs
```

### Partition roles

| Partition | Contents | Who writes it |
|-----------|----------|---------------|
| `factory` | `cyd_boot` recovery environment | One-time factory flash, never overwritten |
| `ota_0` | Full PURR OS (`cyd_s024c` image) | OTA update or SD card install via `cyd_boot` |
| `ota_1` | Any firmware | SD card install via `cyd_boot` |
| `spiffs` | Config, keymaps, crash logs | Running OS + factory recovery |

The factory partition is the **safe anchor**. Even a fully-bricked `ota_0` can be recovered by holding **GPIO0** at power-on to force `cyd_boot`.

---

## Factory Recovery Environment (`cyd_boot`)

`cyd_boot` is a fully self-contained recovery image that fits in the 1 MB factory slot (394 KB used, 62% free). It requires no network connection.

**Entry conditions:**
- GPIO0 held at power-on → always enters recovery UI
- `ota_0` fails to boot 3× in a row (crash-loop detection) → SOS screen
- `ota_0` contains non-PURR firmware or is empty → recovery UI
- Normal boot: `ota_0` valid + no crash loop → fast-path chainload (~20 ms)

**Capabilities:**
- SD card mount (SPI — CS: GPIO5, MOSI: 23, MISO: 19, SCLK: 18)
- Browse `.bin` / `.purr` files from SD root
- Install firmware from SD to any OTA slot with progress bar
- Backup current OTA slot to SD before overwriting
- Restore PURR from SD backup
- Wipe any OTA slot
- SOS crash-loop screen with wipe / boot-anyway / dismiss

---

## Component Structure

As of v0.9.0 all hardware drivers are ESP-IDF components under `CoreOS/components/`:

```
CoreOS/components/
├── drv_display/        — display drivers (ILI9341, ST7789, ST7796, SSD1306, ILI9488)
├── drv_touch/          — touch drivers (CST816S, XPT2046, GT911, MXT336T)
├── drv_bt/             — Bluetooth manager (BLE + Classic)
├── drv_gps/            — GPS UART manager
├── drv_hid/            — USB HID keyboard matrix
├── drv_lora/           — LoRa radio manager + swappable kernels
│   └── kernels/        — SX1262, SX1276, RAK3172 backends
├── drv_wifi/           — WiFi manager (stub for heltec)
├── lib_tftespi/        — TFT_eSPI with self-contained minimal Arduino shim
├── lib_miniwin/        — MiniWin window manager + PURR_CYD HAL
├── lib_radiolib/       — RadioLib (LoRa physical layer)
└── lib_lua/            — Lua scripting runtime
```

---

## Kernel Modules

| Module | CMake Flag | Targets | Notes |
|--------|-----------|---------|-------|
| `wifi_manager` | always on | all | async scan/connect, NVS credentials; stub on heltec |
| `bt_manager` | `PURR_ENABLE_BT` | all | BLE + Classic, NVS paired list |
| `lora_manager` | `PURR_ENABLE_LORA` | heltec, tdeck | swappable backend (drv_lora/kernels/) |
| `purr_mesh` | `PURR_ENABLE_MESH` | heltec, tdeck | Meshtastic co-resident stack (requires LoRa) |
| `mtp_manager` | `PURR_ENABLE_MTP` | all | USB MTP file transfer |
| `flasher` | `PURR_ENABLE_FLASHER` | all | OTA partition flasher module |
| `partition_manager` | CYD always | cyd_s024c, cyd_boot | OTA slot scan, SD card install/backup, wipe |
| `purr_bootloader` | cyd_boot only | cyd_boot | factory image: full recovery UI |
| `power_manager` | always on | all | battery ADC, CPU freq scaling |
| MicroPython runtime | `BUILD_MINI=0` | heltec, cyd_s024c | mpython_runtime + `import kitt` extension |

---

## LoRa Kernels (swappable backends)

`CoreOS/components/drv_lora/kernels/` contains drop-in `lora_manager.h/.cpp` pairs with an identical public API.

| Folder | Radio | Interface |
|--------|-------|-----------|
| `sx1262/` | Semtech SX1262 | SPI — Heltec V3, T-Deck default |
| `rak3172/` | RAK3172 (STM32WL) | UART AT — CattoBoardV1 |
| `sx1276/` | SX1276 / RFM95W | SPI — generic breakout |

---

## UI Shells

### MiniWin (CYD)
All CYD shells run on **MiniWin** (`CoreOS/components/lib_miniwin/`). MiniWin owns the render loop, input routing, Z-ordering, and widget primitives. The PURR-specific HAL port wires MiniWin to `display_ili9341` and `touch_cst816s`.

| Shell | Flag | Target | Description |
|-------|------|--------|-------------|
| Explorer | `PURR_HAS_EXPLORER` | cyd_s024c | Windows CE/PDA — taskbar, overlapping windows |
| BlackberryUI | `PURR_HAS_BLACKBERRY_UI` | cyd_s024c, tdeck | BB-style fixed layout, status bar, app drawer |
| Smol | always | heltec, tdeck | Minimal OLED text shell — 8 rows, app list |

### Smol (Heltec / T-Deck)
Runs directly against `display_ssd1306` with no window manager. 8-row text layout, UP/DOWN/SELECT navigation, app launcher.

---

## MicroPython Runtime

When `BUILD_MINI=0`:
- `mpython_runtime.cpp` — process table (4 slots), exec_app, FreeRTOS task per app
- `kitt_module.c` — `import kitt` C extension, exposes all KITT APIs to Python
- Apps are `.meow` bundles in SPIFFS `/apps/<name>/main.py`

`cyd_boot` is always `BUILD_MINI=1`. MicroPython is never in the factory partition.

---

## System Startup — OTA image (`cyd_s024c`)

```
ESP-IDF bootloader → loads ota_0

main.cpp (pure IDF)
  → kitt.init()
      mount SPIFFS → parse device.json → init display → init touch
      init wifi (background), bt, power → init lora (if enabled)
      init partition_manager → apps_scan() → firmware_scan()
      clear boot_tries counter in NVS   ← signals successful boot
  → system_task (FreeRTOS)
      bridge_start()
      if GPIO0 held LOW → pm_boot_to_factory()   [reboots to factory]
      spawn MiniWin task → launch shell (Explorer / BlackberryUI)
      mpython_init() if BUILD_MINI=0
```

## System Startup — Factory image (`cyd_boot`)

```
ESP-IDF bootloader → loads factory

main.cpp (pure IDF)
  → kitt.init()
      init display_ili9341 + touch_cst816s
      pm_init()  ← mounts SD card (SPI), scans OTA partitions
  → system_task (FreeRTOS)
      bridge_start()
      read GPIO0 → force_bootloader_ui?
      read NVS boot_tries → crash_loop?
      read ota_0 app descriptor → is_purr?

      if is_purr AND !force AND !crash_loop:
          increment boot_tries in NVS
          pm_launch(0)  ← chainloads ota_0, never returns

      else:
          purr_bootloader_start(sos_mode, boot_tries)
              show recovery UI (see Factory Recovery above)
```

---

## Source Tree

```
CoreOS/
├── main/               — IDF main component (CMakeLists, target selection)
├── system/
│   ├── kernel/
│   │   ├── kitt.h/.cpp          — kernel: boot, APIs, lifecycle
│   │   ├── main.cpp             — IDF app_main entry point
│   │   ├── device_config.h      — parsed device.json struct
│   │   ├── idf_compat.c         — IDF 5.x shims
│   │   ├── purr_idf_compat.h    — Serial/GPIO/millis Arduino-API shims (pure IDF)
│   │   └── modules/             — kernel modules (partition_manager, purr_bootloader, …)
│   ├── bridge/
│   │   └── main.cpp             — GPIO → generic keycode translator
│   └── system/
│       └── main.cpp             — system task: shells + OTA chainload logic
├── components/
│   ├── drv_display/             — display drivers
│   ├── drv_touch/               — touch drivers
│   ├── drv_bt/                  — Bluetooth manager
│   ├── drv_gps/                 — GPS manager
│   ├── drv_hid/                 — USB HID keyboard
│   ├── drv_lora/                — LoRa manager + kernels
│   ├── drv_wifi/                — WiFi manager
│   ├── lib_tftespi/             — TFT_eSPI (self-contained, no Arduino dependency)
│   ├── lib_miniwin/             — MiniWin WM + PURR_CYD HAL
│   └── lib_radiolib/            — RadioLib LoRa physical layer
└── partitions_cyd.csv           — 4MB partition table (factory 1MB / ota_0 1.5MB / ota_1 1MB)
```
