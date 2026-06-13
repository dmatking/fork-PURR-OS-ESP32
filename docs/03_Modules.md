# PURR OS — System Modules

System modules are `.purr` binaries of type `PURR_MOD_SYSTEM` or `PURR_MOD_UI`. They are loaded by the kernel at boot from `/flash/modules/` and run as FreeRTOS tasks. Three system modules ship with PURR OS: `driver_manager`, `app_manager`, and `miniwin`.

---

## driver_manager

**Source:** `source/modules/driver_manager/`  
**Type:** `PURR_MOD_SYSTEM`  
**Version:** 0.1.0  
**Required catcalls:** none (it creates them)

The driver manager is the first module the kernel should load. Its job is to find and load every driver blob, validate it, call its `init()`, and let it register its catcall. After `driver_manager_init()` returns, all hardware drivers are up and all catcalls are populated.

### Scan paths

```c
static const char *s_scan_paths[] = {
    "/flash/drivers",    // built-in drivers baked into firmware image
    "/sdcard/drivers",   // hot-loaded drivers from SD card
    NULL
};
```

Paths are scanned in order. First match wins (flash takes precedence over SD).

### Load sequence for each `.purr` driver found

1. `fread` header, validate magic
2. Check `kernel_min` — if KITT too old, mark `[SKIP]`
3. Check `kernel_max` — if beyond ceiling, run `compat_check()`:
   - Walk each `CATCALL_FLAG_*` in `required_catcalls`
   - Check `purr_kernel_display()` etc. — `NULL` means missing
   - All present → `[COMPAT]`. Any missing → `[FAIL]`
4. Call `hdr.init()` — driver registers its catcall inside here
5. Store in `drv_entry_t` registry with final status badge

### Status badges

| Badge | Meaning |
|-------|---------|
| `[OK]` | Loaded cleanly |
| `[COMPAT]` | Beyond kernel_max, all required catcalls still present |
| `[FAIL]` | Required catcall missing, or `init()` returned error |
| `[SKIP]` | Kernel too old for this driver |

### Public API (for UI / driver status screen)

```c
int              driver_manager_get_count(void);
const drv_entry_t *driver_manager_get_entry(int idx);
const char       *drv_status_badge(drv_status_t s);
```

`drv_entry_t` holds name, version, type, status, and a fail_reason string for `[FAIL]` entries. The app_manager (or a dedicated drivers app) can read this to display a driver status screen.

### Module header

```c
purr_module_header_t purr_module = {
    .module_type       = PURR_MOD_SYSTEM,
    .name              = "driver_manager",
    .required_catcalls = 0,    // no catcalls needed — we create them
    .provided_catcalls = 0,    // drivers provide catcalls, not the manager itself
    .init              = driver_manager_init,
    .deinit            = driver_manager_deinit,
};
```

---

## app_manager

**Source:** `source/modules/app_manager/`  
**Type:** `PURR_MOD_SYSTEM`  
**Version:** 0.1.0  
**Required catcalls:** none at load time (display/touch accessed at runtime when launcher opens)

The app manager scans for user apps, maintains the registry, and provides the Cat Apps launcher UI. It is the bridge between the filesystem and the user.

### App scan paths

```c
static const char *s_scan_paths[] = {
    "/flash/apps",
    "/sdcard/apps",
    NULL
};
```

### App tiers

| Extension | Tier | Launch method |
|-----------|------|--------------|
| `.meow` | Lua script | Lua VM (engine module must be loaded) |
| `.paws` | Compiled userland | Dynamic loader (planned) |
| `.claw` | Compiled kernel-access | Dynamic loader (planned) |

The app manager detects the tier from the file extension. Display names are derived by stripping the path prefix and extension from the filename.

### Registry API

```c
int              app_manager_count(void);
const app_entry_t *app_manager_get(int idx);
int              app_manager_launch_idx(int idx);
int              app_manager_launch_path(const char *path);
void             app_manager_stop(int idx);
void             app_manager_open_launcher(void);
int              app_manager_scan(void);   // hot-reload from SD
```

### app_entry_t

```c
typedef struct {
    char        name[48];       // display name
    char        path[128];      // full filesystem path
    app_tier_t  tier;           // MEOW / PAWS / CLAW
    app_state_t state;          // IDLE / RUNNING / STOPPED / ERROR
    char        error[64];      // populated on ERROR
} app_entry_t;
```

### Cat Apps launcher

`app_manager_open_launcher()` is the hook that opens the Cat Apps grid UI. It is called by MiniWin (or any module with display access) to present the app launcher. The actual UI rendering is done through the display catcall — the app_manager itself contains no rendering code.

### Launch status

Currently `.meow` launch returns error "lua runtime not loaded" until the Lua engine module is implemented. `.paws` and `.claw` return "dynamic loader not yet implemented" — in the precompiled model these apps are statically linked into the firmware, so the dynamic path is for future hot-loading from SD.

---

## miniwin

**Source:** `source/modules/miniwin/`  
**Type:** `PURR_MOD_UI`  
**Version:** 0.1.0  
**Required catcalls:** `CATCALL_FLAG_DISPLAY` (display is mandatory; touch is optional — graceful degradation)

MiniWin is an MIT-licensed embedded window manager by John Blaiklock. PURR OS wraps it as a `.purr` UI module. The upstream source is vendored in `MiniWin/` and is untouched. All PURR OS integration lives exclusively in `hal/purr_os/`.

### Directory layout

```
source/modules/miniwin/
  MiniWin/                  upstream source (MIT, John Blaiklock — do not modify)
    miniwin.c/.h            core window manager
    miniwin_touch.c         touch event processing
    miniwin_settings.c      calibration + settings persistence
    gl/                     graphics library (fonts, bitmaps, drawing primitives)
    ui/                     UI controls (buttons, labels, scroll bars, tabs, etc.)
    hal/                    upstream HAL implementations (DevKitC, Linux, STM32, etc.)
  hal/purr_os/              PURR OS HAL (the only files we own/modify)
    hal_lcd.c               routes mw_hal_lcd_* through catcall_display_t
    hal_touch.c             routes mw_hal_touch_* through catcall_touch_t
    hal_delay.c             vTaskDelay + esp_rom_delay_us
    hal_timer.c             FreeRTOS software timer (20ms tick for mw_tick_counter)
    hal_non_vol.c           NVS namespace "miniwin" for calibration data
  miniwin_config.h          unified config — runtime dimensions via catcall
  miniwin_module.c          .purr entry point + miniwin_task
  module.pcat               manifest
```

### How the HAL works

MiniWin was designed with a clean HAL separation. All platform-specific code lives in `hal/`. The old DevKitC HAL hardcoded SPI pins and drove ILI9341 directly. The new `purr_os` HAL routes everything through the kernel's catcall accessors:

```
mw_hal_lcd_filled_rectangle(x,y,w,h,color)
    → purr_kernel_display()->fill_rect(x,y,w,h, rgb888_to_rgb565(color))

mw_hal_touch_get_state()
    → purr_kernel_touch()->is_pressed() ? DOWN : UP

mw_hal_touch_get_point(&x,&y)
    → purr_kernel_touch()->read_point(&tx,&ty)
```

This means miniwin works with any display and any touch driver that has registered a catcall. No per-device configuration or compile-time flags needed.

### miniwin_config.h — unified config

The old architecture had per-device `miniwin_config.h` files because `MW_ROOT_WIDTH` and `MW_ROOT_HEIGHT` were compile-time constants. In PURR OS v0.12, these are runtime calls:

```c
#define MW_ROOT_WIDTH   mw_hal_lcd_get_display_width()
#define MW_ROOT_HEIGHT  mw_hal_lcd_get_display_height()
```

`mw_hal_lcd_get_display_width()` calls `purr_kernel_display()->get_info()` which returns the actual registered display dimensions. One config file works for all devices.

### miniwin_task

The module spawns a FreeRTOS task with 8KB stack:

```c
static void miniwin_task(void *arg) {
    mw_hal_non_vol_init();
    mw_hal_timer_init();      // starts 20ms FreeRTOS timer for mw_tick_counter
    mw_hal_lcd_init();        // reads display dimensions from catcall
    mw_hal_touch_init();      // no-op — touch already initialised by driver
    mw_init();                // MiniWin window manager init
    while (1) {
        mw_process_message(); // MiniWin message pump
        taskYIELD();
    }
}
```

### Touch without a touch driver

If no touch catcall is registered, `mw_hal_touch_get_state()` always returns `MW_HAL_TOUCH_STATE_UP` and `mw_hal_touch_get_point()` returns `false`. MiniWin starts and renders normally — you just can't interact by touch. Keyboard/trackball input via `catcall_input_t` is unaffected.

### Updating MiniWin

To update the upstream source:
1. Replace `MiniWin/` with the new version
2. Do **not** touch `hal/purr_os/` — that's our patch set
3. Do **not** touch `miniwin_config.h` or `miniwin_module.c`
4. Rebuild

---

## Module Registration Summary

| Module | Type | Provided catcalls | Required catcalls |
|--------|------|------------------|------------------|
| `driver_manager` | SYSTEM | — | — |
| `app_manager` | SYSTEM | — | — (runtime) |
| `miniwin` | UI | — | DISPLAY (TOUCH optional) |
