# PURR OS — Architecture Overview
**v0.6.0 / KITT v0.3.0**

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
│  MiniWin Window Manager  (cyd/cyd_boot only)            │
│    PURR_CYD HAL → display_ili9341 + touch_cst816s       │
├─────────────────────────────────────────────────────────┤
│  KITT Kernel (C++)                                      │
│    wifi  bt  lora  power  partition_manager             │
│    purr_bootloader (factory image only)                 │
├─────────────────────────────────────────────────────────┤
│  ESP-IDF 5.3.5 + arduino-esp32 3.1.x  +  FreeRTOS      │
└─────────────────────────────────────────────────────────┘
```

---

## Supported Targets

| Target | Chip | Display | Touch | Notes |
|--------|------|---------|-------|-------|
| `heltec` | ESP32-S3 | SSD1306 128×64 OLED | — | WiFi + LoRa, Smol shell |
| `cyd` | ESP32-2432S024C | ILI9341 2.4" 320×240 | CST816S cap | Full OS → ota_0 |
| `cyd_boot` | ESP32-2432S024C | ILI9341 2.4" 320×240 | CST816S cap | Factory recovery → factory |
| `tdeck` | ESP32-S3 | ST7789 (WIP) | trackball | WiFi + LoRa |

---

## CYD Partition Layout

```
nvs        20 KB    0x9000   — NVS key-value store
otadata     8 KB    0xe000   — OTA boot selection register
factory     1 MB    0x10000  — cyd_boot: recovery bootloader (never touched by OTA)
ota_0       1.5 MB  0x110000 — cyd: full PURR OS (OTA-updatable)
ota_1       1 MB    0x290000 — generic slot (any third-party firmware)
spiffs    448 KB    0x390000 — filesystem: device.json, app data, logs
```

The factory partition is flashed **once** and never overwritten. It holds `cyd_boot` — a minimal bootloader image that scans OTA slots and lets you boot, wipe, or install firmware.

Hold **GPIO0** while the full OS is running → `pm_boot_to_factory()` → reboots to factory.

---

## Kernel Modules

| Module | CMake Flag | Targets | Notes |
|--------|-----------|---------|-------|
| `wifi_manager` | always on | all | async scan/connect, NVS credentials |
| `bt_manager` | `PURR_ENABLE_BT` | all | BLE + Classic, NVS paired list |
| `lora_manager` | `PURR_ENABLE_LORA` | heltec, tdeck | swappable backend (LoRa Kernels/) |
| `purr_mesh` | `PURR_ENABLE_MESH` | heltec, tdeck | Meshtastic co-resident stack (requires LoRa) |
| `mtp_manager` | `PURR_ENABLE_MTP` | all | USB MTP file transfer |
| `flasher` | `PURR_ENABLE_FLASHER` | all | OTA partition flasher module |
| `partition_manager` | CYD always | cyd, cyd_boot | OTA slot scan, boot selection, wipe |
| `purr_bootloader` | cyd_boot only | cyd_boot | factory image: generic slot scanner UI |
| `display_ili9341` | CYD always | cyd, cyd_boot | TFT_eSPI-backed 320×240 ILI9341 driver |
| `display_ssd1306` | Heltec always | heltec, tdeck | Adafruit SSD1306 128×64 OLED driver |
| `touch_cst816s` | CYD always | cyd, cyd_boot | CST816S capacitive touch, I2C, landscape coords |
| `power_manager` | always on | all | battery ADC, CPU freq scaling |
| MicroPython runtime | `BUILD_MINI=0` | heltec, cyd, tdeck | mpython_runtime + `import kitt` extension |

---

## LoRa Kernels (swappable backends)

`LoRa Kernels/` contains drop-in `lora_manager.h/.cpp` pairs with an identical public API. Copy any folder into `CoreOS/system/kernel/modules/` to switch radios. The SDK does this automatically via `--lora-kernel`.

| Folder | Radio | Interface |
|--------|-------|-----------|
| `SX1262/` | Semtech SX1262 | SPI — Heltec V3, T-Deck default |
| `RAK3172/` | RAK3172 (STM32WL) | UART AT — CattoBoardV1 |
| `SX1276_RFM95W/` | SX1276 / RFM95W | SPI — generic breakout |

---

## UI Shells

### MiniWin (CYD)
All CYD shells run on **MiniWin** (`CoreOS/components/miniwinwm/`, MIT licensed). MiniWin owns the render loop, input routing, Z-ordering, and widget primitives. Shells are MiniWin applications. The PURR-specific HAL port at `MiniWin/hal/PURR_CYD/` wires MiniWin to `display_ili9341` and `touch_cst816s`.

| Shell | `PURR_SHELL` flag | Target | Description |
|-------|-----------------|--------|-------------|
| Explorer | `explorer` | cyd | Windows CE/PDA — taskbar, overlapping windows, Start launcher |
| BlackberryUI | `blackberry` | cyd, tdeck | BB-style — fixed layout, status bar, app drawer |
| Both | `both` (default) | cyd | Explorer preferred, BlackberryUI compiled as fallback |
| Smol | always | heltec, tdeck | Minimal OLED text shell — 8 rows, app list, PURR menu |

### Smol (Heltec / T-Deck)
Runs directly against `display_ssd1306` with no window manager. 8-row text layout, UP/DOWN/SELECT navigation, app launcher.

---

## MicroPython Runtime

When `BUILD_MINI=0`:
- `mpython_runtime.cpp` — process table (4 slots), exec_app, FreeRTOS task per app
- `kitt_module.c` — `import kitt` C extension, exposes all KITT APIs to Python
- Apps are `.meow` bundles in SPIFFS `/apps/<name>/main.py`

When `BUILD_MINI=1` (or `--mini`): MicroPython is excluded entirely. All C++ shells still work. `cyd_boot` is always mini.

---

## System Startup — OTA image (cyd / heltec / tdeck)

```
main.cpp (Arduino)
  → kitt.init()
      mount SPIFFS → parse device.json → init display → init touch (CYD)
      init wifi, bt, power → init lora (if enabled)
      init partition_manager (CYD) → apps_scan() → firmware_scan()
      write KITT_READY to NVS
  → system_task (FreeRTOS)
      bridge_start()
      if GPIO0 held LOW → pm_boot_to_factory()   [CYD only]
      spawn MiniWin task (CYD)
      launch shell (Explorer / BlackberryUI / Smol)
      mpython_init() if BUILD_MINI=0
```

## System Startup — Factory image (cyd_boot)

```
main.cpp (Arduino)
  → kitt.init()
      init display_ili9341 + touch_cst816s + partition_manager
  → system_task (FreeRTOS)
      bridge_start()
      purr_bootloader_start()
          scan OTA slots via esp_ota_get_partition_description()
          render slot cards: firmware version + BOOT / WIPE / INSTALL buttons
          handle tap → confirm dialog → execute action
          blue LED heartbeat
```

---

## Source Tree

```
CoreOS/system/
├── kernel/
│   ├── kitt.h/.cpp          — kernel: boot, APIs, lifecycle
│   ├── main.cpp             — Arduino entry point
│   ├── device_config.h      — parsed device.json struct
│   ├── idf_compat.c         — IDF 5.x compat shims
│   ├── devices/             — per-target JSON profiles
│   └── modules/             — optional kernel modules (see table above)
├── bridge/
│   ├── main.cpp             — GPIO→generic keycode translator
│   └── keymaps/             — per-device JSON keymap files
├── system/
│   └── main.cpp             — system task: boots correct shell per image type
└── micropython/
    ├── mpython_runtime.h/.cpp  — MicroPython process table + KITT bridge
    └── kitt_module.c           — `import kitt` extension module

CoreOS/components/
├── TFT_eSPI/                — ILI9341 driver (cyd targets)
└── miniwinwm/               — MiniWin WM + PURR_CYD HAL port
    └── MiniWin/hal/PURR_CYD/
        ├── hal_lcd.cpp      — draws via display_ili9341
        ├── hal_touch.cpp    — reads via touch_cst816s
        ├── hal_timer.cpp    — esp_timer 20ms tick
        ├── hal_delay.cpp    — Arduino delay/delayMicroseconds
        └── hal_non_vol.cpp  — NVS for calibration storage
```
