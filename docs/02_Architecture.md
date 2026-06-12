# PURR OS — Architecture

## Boot sequence

```
ROM bootloader
  └─ ESP-IDF bootloader (0x0 / 0x1000)
       └─ purr_os_core (0x10000)
            └─ app_main() → Kitt::boot()
                 ├─ 1. Panic handler register
                 ├─ 2. NVS init
                 ├─ 3. device_config_default() — baked-in compile-time config
                 ├─ 4. drv_registry_init() — register sys drivers
                 ├─ 5. hal_input_init() (if HAS_HID)
                 ├─ 6. partition_manager_init() (if HAS_PARTITION_MGR)
                 ├─ 7. display_init() + splash
                 ├─ 8. power_manager_init()
                 ├─ 9. wifi_manager_init() (if HAS_WIFI)
                 ├─ 10. bt_manager_init() (if HAS_BT)
                 ├─ 11. lora_manager_init() (if HAS_LORA)
                 ├─ 12. gps_manager_init() (if HAS_GPS)
                 └─ 13. mw_user_init() → shell launch
```

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
