# PURR OS — Architecture

## Big Picture

```
┌─────────────────────────────────────────────────────────────┐
│                        PURR OS Boot                         │
│                                                             │
│  app_main()                                                 │
│    │                                                        │
│    ├── NVS init                                             │
│    ├── mount /flash (SPIFFS)                                │
│    ├── purr_kernel_scan_modules("/flash/modules")           │
│    │     ├── load driver_manager.purr → registers catcalls  │
│    │     │     display ──► catcall_display_t slot           │
│    │     │     touch   ──► catcall_touch_t slot             │
│    │     │     input   ──► catcall_input_t slot             │
│    │     │     radio   ──► catcall_radio_t slot             │
│    │     │     gps     ──► catcall_gps_t slot               │
│    │     ├── load miniwin.purr  → spawns miniwin_task       │
│    │     └── load app_manager.purr → scans + registers apps │
│    │                                                        │
│    └── idle (vTaskDelay forever)                            │
│                                                             │
│  Everything from here is driven by module tasks.           │
└─────────────────────────────────────────────────────────────┘
```

The kernel's idle loop is the last thing `boot.c` does. All work happens in FreeRTOS tasks created by modules.

---

## Kernel Spine

**Source:** `source/kernel/core/`

The kernel spine is intentionally the smallest possible thing that can load modules and host a catcall registry. It has four responsibilities and nothing else:

### 1. Flash VFS mount

```c
esp_vfs_spiffs_conf_t conf = {
    .base_path = "/flash",
    ...
};
esp_vfs_spiffs_register(&conf);
```

All flash-resident content is accessible at `/flash/`. Module blobs live at `/flash/modules/`, drivers at `/flash/drivers/`, apps at `/flash/apps/`.

### 2. NVS init

NVS is initialised before any module runs. If the partition is corrupt it is erased and re-initialised. Modules (especially miniwin, for touch calibration) call `nvs_open()` directly without re-initialising.

### 3. Module scanner + loader

`purr_kernel_scan_modules(dir)` opens a directory, finds every `.purr` file, and calls `purr_kernel_load_module(path)` on each.

`purr_kernel_load_module(path)` does:
1. `fread` the first `sizeof(purr_module_header_t)` bytes
2. Validate `magic == 0x50555252` ('PURR')
3. Validate `abi_version == PURR_MODULE_ABI_VERSION`
4. Check `kernel_min` — if current KITT < kernel_min, log `[SKIP]` and stop
5. Check `kernel_max` — if set and KITT > kernel_max, run compat check:
   - Walk `required_catcalls` bitmask
   - All present → proceed with `[COMPAT]` badge
   - Any missing → log `[FAIL]`, stop
6. Call `hdr.init()`. Non-zero return → log `[FAIL]`
7. Store a copy of the header in the module registry (32 slots)

### 4. Catcall registry

Five static slots, one per catcall type:

```c
static const catcall_display_t *s_display = NULL;
static const catcall_touch_t   *s_touch   = NULL;
static const catcall_input_t   *s_input   = NULL;
static const catcall_radio_t   *s_radio   = NULL;
static const catcall_gps_t     *s_gps     = NULL;
```

Drivers call `purr_kernel_register_display(ptr)` etc. to register themselves. **Last registered wins** — this allows hot replacement. Everything else calls `purr_kernel_display()` etc. to get the current pointer. If no driver is registered, returns `NULL`. All callers must null-check.

---

## The `.purr` Module ABI

**Source:** `source/kernel/core/purr_module.h`

Every `.purr` binary exports one symbol: `purr_module` of type `purr_module_header_t`.

```c
typedef struct {
    uint32_t magic;             // 0x50555252 ('PURR') — must match
    uint8_t  abi_version;       // must match PURR_MODULE_ABI_VERSION (1)
    uint8_t  module_type;       // PURR_MOD_DRIVER / SYSTEM / UI / APP
    char     name[32];          // human-readable name
    char     version[12];       // semver string "0.1.0"
    char     kernel_min[12];    // minimum KITT version required
    char     kernel_max[12];    // maximum KITT version supported ("" = no ceiling)
    uint32_t provided_catcalls; // bitmask of CATCALL_FLAG_* this module registers
    uint32_t required_catcalls; // bitmask of CATCALL_FLAG_* this module needs
    int    (*init)(void);       // called at load — return 0 for success
    void   (*deinit)(void);     // called if module is unloaded
} purr_module_header_t;
```

### Module types

| Constant | Value | Meaning |
|----------|-------|---------|
| `PURR_MOD_DRIVER` | 0x01 | Hardware driver — registers catcalls |
| `PURR_MOD_SYSTEM` | 0x02 | System service — driver_manager, app_manager |
| `PURR_MOD_UI` | 0x03 | UI framework — miniwin |
| `PURR_MOD_APP` | 0x04 | App — reserved for future use |

### Catcall bitmask flags

| Flag | Value | Catcall |
|------|-------|---------|
| `CATCALL_FLAG_DISPLAY` | `1<<0` | Display |
| `CATCALL_FLAG_TOUCH` | `1<<1` | Touch |
| `CATCALL_FLAG_INPUT` | `1<<2` | Input (keyboard/trackball) |
| `CATCALL_FLAG_RADIO` | `1<<3` | Radio (LoRa/sub-GHz) |
| `CATCALL_FLAG_GPS` | `1<<4` | GPS |

### Version compat logic

```
kernel_min = "0.9.0"   → module needs at least KITT 0.9.0
kernel_max = ""        → no ceiling, always try to load
kernel_max = "0.12.0"  → if KITT > 0.12.0, run compat check
```

Compat check: walk `required_catcalls` bitmask. All present → `[COMPAT]`. Any missing → `[FAIL]`.

### Runtime status badges

| Badge | Meaning |
|-------|---------|
| `[OK]` | Loaded cleanly, within version range |
| `[COMPAT]` | Beyond `kernel_max`, but all required catcalls present |
| `[FAIL]` | Required catcall missing — not loaded |
| `[SKIP]` | Kernel too old (below `kernel_min`) |

---

## Catcall System

Catcalls are the kernel's interface contracts — PURR OS's version of syscalls. They are simple C structs of function pointers. Each defines a capability: display, touch, input, radio, GPS.

Drivers implement these structs and register them with the kernel. Everything else calls through them.

**The kernel never calls a driver directly.** It stores a pointer and that's it.

```
Driver registers:     purr_kernel_register_display(&my_display_catcall)
Miniwin calls:        purr_kernel_display()->fill_rect(0, 0, 320, 240, 0x0000)
App calls:            purr_kernel_display()->push_pixels(x, y, w, h, data)
```

See [02_Catcalls.md](02_Catcalls.md) for the full interface spec for all five catcalls.

---

## Module Load Order

Modules are loaded in filesystem order within `/flash/modules/`. The intended boot order is:

1. `driver_manager.purr` — scans `/flash/drivers` + `/sdcard/drivers`, loads display + touch drivers, registers catcalls
2. `miniwin.purr` — requires display catcall; spawns miniwin_task
3. `app_manager.purr` — scans apps, populates registry, waits for miniwin before showing Cat Apps UI

The kernel does not enforce this order — it is the responsibility of `modulestrap` to name blobs so they sort correctly, or of `purrstrap` to write a manifest that specifies order explicitly (planned).

---

## Source File Map

```
source/kernel/
  catcalls/
    catcall_display.h     display interface (init, push_pixels, fill_rect, brightness, info, deinit)
    catcall_touch.h       touch interface (init, read_point, is_pressed, deinit)
    catcall_input.h       input interface (init, poll_event, deinit)
    catcall_radio.h       radio interface (init, send, receive, rssi, snr, set_freq, set_power, deinit)
    catcall_gps.h         GPS interface (init, get_fix, deinit)
    catcalls.h            master include for all five
  core/
    boot.c                app_main — NVS, VFS, scan modules, idle
    purr_kernel.h         public kernel API
    purr_kernel.c         catcall registry + module loader implementation
    purr_module.h         .purr binary ABI struct
    README.md             kernel spine responsibilities
```
