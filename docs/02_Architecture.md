# Architecture

---

## Component graph

```
CoreOS/
├── main/                        ← IDF "main" component
│   └── CMakeLists.txt           ← feature flags, source selection
│
└── components/
    ├── lib_miniwin/             ← MiniWin WM (C) + PURR HAL glue (C++)
    ├── drv_display/             ← ILI9341 / ST7789 / ST7796 / SSD1306
    ├── drv_touch/               ← CST816S / XPT2046 / GT911
    ├── drv_wifi/                ← WiFi station/AP manager
    ├── drv_bt/                  ← Bluetooth manager
    ├── drv_lora/                ← SX1262 / SX1276 LoRa
    ├── drv_shell/               ← USB serial REPL (esp_console)
    ├── lib_lua/                 ← Lua 5.4 (ESP32 LUA_32BITS=1)
    ├── lib_tftespi/             ← TFT_eSPI shim (display low-level)
    └── ...

system/
└── kernel/
    ├── main.cpp                 ← FreeRTOS task entry, KITT init
    ├── kitt.h / kitt.cpp        ← KITT kernel singleton
    ├── device_config.h/cpp      ← JSON device config loader
    ├── purr_panic.h/cpp         ← Blue/red screen panic handler
    ├── purr_version.h           ← Version defines
    └── modules/
        ├── partition_manager    ← OTA + SD card firmware management
        ├── lua_runtime          ← Global Lua 5.4 runtime (KITT-level)
        ├── power_manager        ← Battery / CPU freq
        └── ui_stubs             ← No-op stubs for disabled features

devices/
├── cyd/                         ← CYD S024C MiniWin HAL
│   ├── hal_lcd.cpp              ← ILI9341 via SPI2 (HSPI)
│   ├── hal_touch.cpp            ← CST816S via I2C
│   ├── hal_timer/delay/...
│   └── purr_app.cpp             ← WCE shell (mw_user_init)
├── cyd_s028r/                   ← CYD S028R variant (XPT2046 touch)
├── jc3248/ tdeck_plus/ ...      ← Other device HALs
└── apps/                        ← Shared cross-device app windows
    ├── purr_app_catalog.h/cpp   ← Built-in app registry
    ├── purr_taskbar.h/cpp       ← Taskbar state
    ├── app_settings.cpp         ← Settings app
    ├── app_files.cpp            ← File Explorer
    ├── app_launcher.cpp         ← SD app launcher
    ├── app_lua_window.h/cpp     ← Lua script window host
    ├── shell_blackberry.cpp     ← Blackberry shell (PURR_THEME_BLACKBERRY)
    ├── purr_wm_launch.cpp       ← purr_wm_launch() MiniWin impl
    └── ...
```

---

## Boot sequence

```
esp_restart / power-on
    │
    ├── ROM bootloader → 2nd stage bootloader
    │
    ├── factory slot (0x10000)   ← cyd_boot image
    │   └── Checks NVS for pending OTA → boots ota_0 or ota_1
    │
    └── ota_0 / ota_1 (0x120000 / 0x290000)  ← PURR OS image
        │
        ├── app_main() → FreeRTOS scheduler start
        ├── kitt.init()
        │   ├── device_config_load("/spiffs/cyd.json")
        │   ├── display init → boot splash
        │   ├── touch init
        │   ├── NVS init
        │   ├── WiFi manager init
        │   ├── [optional] BT / LoRa / Shell init
        │   ├── pm_init()  ← SD card mount, OTA scan
        │   └── [optional] lua_runtime_init()
        │
        └── mw_init() → mw_user_init()   ← shell takes over
            └── shell creates root window → mw_task() loop
```

---

## CYD partition layout

```
 Addr       Size    Label      Contents
 0x000000   4KB     nvs        NVS (WiFi credentials, NVS config)
 0x008000   4KB     partition  Partition table
 0x010000   1.06MB  factory    PURR factory bootloader (cyd_boot)
 0x120000   1.44MB  ota_0      PURR OS (primary slot)
 0x290000   1.00MB  ota_1      OTA update target slot
 0x390000   448KB   spiffs     Read-only assets (JSON configs, fonts)
```

---

## MiniWin WM model

MiniWin is a single-threaded window manager. All drawing happens in one FreeRTOS task.

- **Root window**: created by the shell (`mw_user_init()`), full-screen, no title bar, no close button. Receives all touch events not consumed by an overlaid app window.
- **App windows**: created by `mw_add_window()` with a title bar and close button. Layered on top of the root. Touch is routed to the topmost window that contains the touch point.
- **Paint callbacks**: `void paint(mw_handle_t, const mw_gl_draw_info_t *)` — called by MiniWin when the window needs redrawing. Only draw within `draw_info` clip bounds.
- **Message callbacks**: `void message(const mw_message_t *)` — receives `MW_TOUCH_DOWN_MESSAGE`, `MW_TIMER_MESSAGE`, `MW_WINDOW_CREATED_MESSAGE`, `MW_WINDOW_REMOVED_MESSAGE`, etc.
- **Touch coordinates**: `MW_TOUCH_DOWN_MESSAGE` delivers coordinates **already in client space** — `message_data >> 16` = client_x, `message_data & 0xFFFF` = client_y. No offset subtraction needed.

---

## Feature flag system

Feature flags are set at CMake configure time and propagated as C preprocessor defines:

| CMake variable | C define | Effect |
|----------------|----------|--------|
| `PURR_ENABLE_LUA=1` | `PURR_HAS_LUA` | Compiles Lua runtime + app_lua_window |
| `PURR_ENABLE_BT=1` | `PURR_HAS_BT` | Compiles drv_bt, enables KITT BT API |
| `PURR_ENABLE_LORA=1` | `PURR_HAS_LORA` | Compiles drv_lora, enables KITT LoRa API |
| `PURR_ENABLE_MAGIDOS=1` | `PURR_HAS_MAGIDOS` | Adds MagiDOS to app catalog |
| `PURR_ENABLE_MAGICMAC=1` | `PURR_HAS_MAGICMAC` | Adds MagicMac to app catalog |
| `PURR_UI_KERNEL=miniwin` | `PURR_HAS_MINIWIN` | Compiles MiniWin WM |
| `PURR_UI_THEME=blackberry` | `PURR_THEME_BLACKBERRY` | Activates Blackberry shell |

Defines are propagated to both `main` and `lib_miniwin` components so all app code sees them.
