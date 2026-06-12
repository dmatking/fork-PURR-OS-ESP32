# PURR OS — Architecture

## Boot sequence

```
ROM bootloader
  └─ ESP-IDF bootloader (0x1000 on ESP32 / 0x0 on ESP32-S3)
       └─ purr_os_core (0x10000)
            └─ app_main()
                 ├─ setup()
                 │    ├─ Kitt::init()
                 │    │    ├─ 1.  Panic handler register
                 │    │    ├─ 2.  NVS init (flash)
                 │    │    ├─ 3.  device_config_default() — baked-in compile-time config
                 │    │    ├─ 4.  sys_drv_register_all() — register all hardware drivers
                 │    │    ├─ 5.  sys_drv_init_all() — sequenced init (display first)
                 │    │    ├─ 6.  WiFi manager init (if HAS_WIFI)
                 │    │    ├─ 7.  BT manager init (if HAS_BT)
                 │    │    ├─ 8.  LoRa manager init (if HAS_LORA)
                 │    │    ├─ 9.  GPS manager init (if HAS_GPS)
                 │    │    ├─ 10. SD card / partition manager (if HAS_PARTITION_MGR && cfg.sd)
                 │    │    └─ 11. KITT ready
                 │    ├─ lua_runtime_init() (if HAS_LUA)
                 │    ├─ purr_input_init()
                 │    ├─ purr_drv_init()
                 │    ├─ mw_init() (if HAS_MINIWIN)
                 │    ├─ purr_shell_start() (if HAS_SHELL)
                 │    └─ system_start()
                 └─ loop_task (pinned to core 0)
                      └─ kitt.update() + purr_drv_tick() + mw_process_message()
```

**SD card:** `cfg.sd` is set in `device_config_default()` in `device_config.cpp`. If it's `false`, `pm_init()` is never called regardless of what `device.json` says — the baked config wins.

## Device config — compile-time baked

All device config is baked into firmware at compile time via `PURR_TARGET_*` defines.  
No `device.json` file is required. `device_config_default()` in [CoreOS/system/kernel/device_config.cpp](../CoreOS/system/kernel/device_config.cpp) contains the full `#ifdef` tree.

The legacy `device_config_load()` (SPIFFS JSON) still exists but is never called.

## Driver registry

Sys drivers are registered in [CoreOS/system/kernel/modules/purr_drv_registry.cpp](../CoreOS/system/kernel/modules/purr_drv_registry.cpp).  
Each driver registers itself with a name, subsystem, and enable flag visible in the Drivers app (SYS tab).

Notable guard:
```cpp
// GT911 is owned by hal_touch.cpp on tdeck_plus (shared I2C with keyboard)
#if defined(PURR_HAS_TOUCH_GT911) && !defined(PURR_DEVICE_TDECK_PLUS)
    touch_gt911_drv_register(true);
#endif
```

## HAL overlay pattern

Each device in `devices/<target>/` provides the MiniWin HAL:
- `hal_lcd.cpp` — display init + pixel write
- `hal_touch.cpp` — touch state read
- `hal_delay.cpp` — `mw_hal_delay_ms()`
- `hal_timer.cpp` — `mw_hal_timer_get_ms()`
- `hal_non_vol.cpp` — NVS read/write for calibration
- `purr_app.cpp` — `mw_user_init()` entry point
- `miniwin_config.h` — screen dimensions, color depth, feature flags

The `ui/lib_miniwin/CMakeLists.txt` maps `TARGET_DEVICE` → `devices/<folder>` and compiles the right HAL files automatically.

## MiniWin shell (tdeck_plus)

`purr_app.cpp` creates a full-screen shell window:

```cpp
void mw_user_init(void) {
    hal_input_init();
    // shell window covers full screen, receives touch + key focus
    shell_handle = mw_add_window(&r, "",
        shell_paint, shell_message, NULL, 0,
        MW_WINDOW_FLAG_IS_VISIBLE | MW_WINDOW_FLAG_TOUCH_FOCUS_AND_EVENT, NULL);
    hal_input_set_shell_handle(shell_handle);
    mw_paint_all();
}
```

MiniWin's 3-point calibration (`mw_touch_calibrate()`) runs automatically on first boot when `!mw_settings_is_calibrated()`.  
Calibration is stored in NVS.

## SD card — T-Deck Plus

The T-Deck Plus shares SPI3 between the display (CS=12) and SD card (CS=39).

- MOSI=41, MISO=38, SCLK=40
- GPIO 10: peripheral power enable — must be driven HIGH before SPI init
- SD uses SPI mode (not SDIO). The IDF `sdmmc_io.c` patch allows `ESP_ERR_INVALID_SIZE` in SPI mode (non-SDIO card CMD52 response).

## Kernel panic

`purr_panic(stop_code, level, msg)` in `CoreOS/system/kernel/purr_panic.cpp`.

Two levels:
- **PURR_PANIC_BLUE** (`:-/`) — recoverable warning. Draws blue screen, logs to serial, returns. The calling code may continue.
- **PURR_PANIC_RED** (`ยทยท(`) — fatal. Draws red screen, waits 10 seconds, calls `esp_restart()`.

**Escalation rule:** A static counter (`s_blue_count`) tracks blue panics within the current uptime session (resets on every power cycle or hard reboot — no NVS involved). On the **third** blue panic, it is automatically escalated to red: 10-second red screen then forced reboot.

```
Blue panic #1 → blue screen shown, dismissed, counter = 1
Blue panic #2 → blue screen shown, dismissed, counter = 2
Blue panic #3 → counter = 3 → escalates to red → 10s → reboot
```

**Display routing:** When MiniWin is compiled in (`PURR_HAS_MINIWIN`), the panic screen draws through `mw_hal_lcd_*` (the same path as normal rendering). Without MiniWin, it calls the raw `display_*_fill_rect` and `display_*_draw_string` functions directly. The display must already be initialized for the screen to appear — a panic triggered before `display_init()` will print to serial only.

**Stop codes** (defined in `purr_panic.h`):

| Code | Meaning |
|------|---------|
| `CATFAIL` | Critical kernel init failure |
| `DEADBEEF` | Memory corruption detected |
| `APP_CRASH` | Application exception |
| `MEM_FULL` | Heap exhausted |
| `WATCHDOG` | Hardware watchdog timeout |
| `HAL_FAIL` | Hardware abstraction error |

## Power manager

Battery ADC is device-specific:

```cpp
#elif defined(PURR_DEVICE_TDECK_PLUS)
#  define BATT_ADC_PIN  -1   // no exposed battery ADC on T-Deck Plus
#  define BATT_CHG_PIN  -1
```

Blue panic screen activates when free heap drops below 50KB threshold.

## Build system

`purrstrap.py` wraps ESP-IDF's `idf.py` with:
1. Per-device build dirs (`CoreOS/build_<device>/`)
2. Per-device sdkconfig cache (`CoreOS/sdkconfig_<device>`)
3. CMake `-D` flags injected from `.purrstrap` config
4. SPIFFS image generation (spiffsgen.py) after main build
5. Release packing to `baked/<device>/` with `flash.sh`

IDF component search order:
```
CoreOS/main/           (the main component)
CoreOS/components/     (lib_lua, lib_arduino, lib_nanopb, lib_radiolib, lib_mesh_pb)
ui/                    (lib_miniwin, lib_tftespi, purr_wm)
drivers/               (drv_display, drv_touch, drv_wifi, drv_bt, drv_lora, ...)
```
