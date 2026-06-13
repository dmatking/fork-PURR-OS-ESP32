# PURR OS — Build Tools

Three separate tools handle the three distinct build concerns. All output goes to `cattobaked/` at the repo root.

```
purrstrap/       final flashable firmware image per device
modulestrap/     .purr kernel module + driver blobs
catstrap/        user apps (.meow/.paws/.claw) + SDK
```

All three tools accept `--help` for a full option list.

---

## purrstrap

**File:** `purrstrap/purrstrap.py`
**Output:** `cattobaked/<device>/`

purrstrap is the top-level image builder. It:
1. Reads `source/devices/<device>/device.pcat`
2. Calls modulestrap to build all .purr module + driver blobs
3. Calls catstrap to register all app blobs
4. Generates `purr_device_glue.c` with pin #defines and radio flags
5. Assembles a SPIFFS image from the [flash] + [apps] manifest
6. Invokes IDF to compile the kernel spine (if IDF_PATH is set)
7. Merges everything into a single flashable `PURR_OS_<device>.bin`

### Commands

```
purrstrap build <device>       build firmware for a device
purrstrap flash <device> [-p PORT]  build + flash to connected device
purrstrap clean <device>       remove build artifacts for device
purrstrap list                 list all supported devices
purrstrap status               show current workspace config
purrstrap doctor               check environment health
```

### `purrstrap list`

Reads all `source/devices/*/device.pcat` and prints a table of devices, chips, and radio capabilities:

```
device          chip        wifi  bt    lora
cyd             esp32       yes   yes   -
cyd_s024c       esp32       yes   yes   -
cyd_s028r       esp32       yes   yes   -
tdeck           esp32s3     yes   yes   sx1262
tdeck_plus      esp32s3     yes   yes   sx1276
jc3248w535      esp32s3     yes   yes   -
heltec          esp32s3     yes   yes   sx1262
waveshare169    esp32s3     yes   yes   -
```

### `purrstrap doctor`

Checks: `idf.py` in PATH, `python3`, `git`, `IDF_PATH` set and valid, `source/kernel/core/boot.c` exists, `spiffsgen.py` available.

### `purrstrap build <device>`

Full build sequence:
1. Reads `device.pcat`
2. Writes `.purrstrap` workspace JSON
3. Calls `modulestrap build all` → produces .purr blobs
4. Calls `catstrap build all` → registers app blobs
5. Generates `cattobaked/<device>/glue/purr_device_glue.c`
6. Stages files into `cattobaked/<device>/spiffs_staging/`:
   - `modules/` — system modules (.purr)
   - `drivers/<type>/` — driver blobs (.purr)
   - `apps/` — app blobs (.claw/.paws/.meow) or .meta.json registrations
7. Runs `spiffsgen.py` → `cattobaked/<device>/flash.bin`
8. Runs `idf.py set-target` + `idf.py build` on `CoreOS/` → `firmware.bin`
9. Runs `esptool.py merge_bin` → `PURR_OS_<device>.bin`

### `purrstrap flash <device>`

Runs build then flashes:
```bash
esptool.py --port /dev/ttyUSB0 --baud 460800 write_flash <offset> flash.bin
```

Add `-p /dev/ttyACM0` to specify a port.

### Output: `cattobaked/<device>/`

```
PURR_OS_<device>.bin        complete merged flash image
firmware.bin                kernel spine only
bootloader.bin
partition-table.bin
flash.bin                   SPIFFS filesystem image
glue/purr_device_glue.c     generated pin + radio glue
spiffs_staging/             staged files before spiffsgen
build.json                  build metadata (device, versions, timestamps)
```

### Without IDF

If `IDF_PATH` is not set, purrstrap skips the kernel spine build and merge steps. The `flash.bin` SPIFFS image is still produced and can be flashed manually:
```bash
esptool.py write_flash 0x290000 cattobaked/<device>/flash.bin
```

---

## modulestrap

**File:** `modulestrap/modulestrap.py`
**Output:** `cattobaked/modules/`, `cattobaked/drivers/`

modulestrap compiles all `.purr` kernel module and driver blobs. It scans `source/modules/` and `source/drivers/`, generates IDF component `CMakeLists.txt` fragments, and writes `cattobaked/components_manifest.cmake` so CoreOS can include all modules in one IDF build.

### Commands

```
modulestrap build all         register all modules and drivers
modulestrap build modules      register system modules only
modulestrap build drivers      register drivers only
modulestrap build <name>       register one target (e.g. "kittenui", "display/ili9341")
modulestrap list               list all buildable targets
modulestrap list --drivers D   list targets from an external driver directory
modulestrap clean [all]        remove .purr blobs from cattobaked/
```

### What "register" means

modulestrap does not invoke IDF directly (that is purrstrap's job). Instead it:
1. Finds all C source files in the module directory
2. Generates `CMakeLists.txt` as an IDF component fragment (include dirs pointing to `source/kernel/`)
3. Writes `.meta.json` with source list, version, and status="registered"
4. Writes `cattobaked/components_manifest.cmake` listing all EXTRA_COMPONENT_DIRS

When purrstrap later runs `idf.py build` on `CoreOS/`, it includes `components_manifest.cmake` which pulls in every registered module as an IDF component.

### Output structure

```
cattobaked/
  modules/
    app_manager.purr           (placeholder until IDF builds real binary)
    kittenui.purr
    miniwin.purr
    oled_ui.purr
    driver_manager.purr
  drivers/
    display/
      ili9341.purr
      st7789.purr
      axs15231b.purr
      ssd1306.purr
    touch/
      xpt2046.purr
      cst816s.purr
      gt911.purr
    input/
      trackball.purr
    radio/
      sx1262.purr
      sx1276.purr
    gps/
      generic_nmea.purr
  components_manifest.cmake    included by CoreOS/CMakeLists.txt
```

### Custom drivers

Drop a directory containing `driver.pcat` and C source into `user_drivers/` — modulestrap auto-scans it:
```
user_drivers/
  my_sensor/
    driver.pcat
    my_sensor.c
```

Or point to an external directory:
```bash
modulestrap build all --drivers /path/to/my_drivers
```

---

## catstrap

**File:** `catstrap/catstrap.py`
**Output:** `cattobaked/apps/`

catstrap builds user apps (.meow/.paws/.claw) and manages the catstrap SDK (headers for app development).

### Commands

```
catstrap build all              build/register all apps + MagicMac + MagiDOS
catstrap build <name>           build/register one app by name
catstrap build magicmac         build MagicMac (.claw, in-house exclusive)
catstrap build magidos           build MagiDOS (.claw, in-house exclusive)
catstrap validate <file.meow>   syntax-check a Lua script
catstrap sdk info               show SDK version and API surface
catstrap sdk install            copy SDK headers to catstrap/sdk/include/
catstrap list                   list all buildable apps
catstrap clean [all|<name>]     remove app output from cattobaked/apps/
```

### What "build" does for .paws/.claw apps

1. Finds all `.c`/`.cpp` files in the app directory
2. Generates `CMakeLists.txt` as an IDF component fragment (include dirs include `source/kernel/core/` and `source/kernel/catcalls/` for .claw tier)
3. Writes `cattobaked/apps/<name>.<tier>.meta.json` with registration metadata
4. Status: "registered" — included in the next `purrstrap build`

`.meow` scripts are copied directly to `cattobaked/apps/<name>.meow`.

### SDK

The catstrap SDK provides headers that app developers build against. Run `catstrap sdk install` to populate `catstrap/sdk/include/`:

```
catstrap/sdk/include/
  purr_sdk.h              tier gate — includes the right sub-header
  purr_sdk_paws.h         .paws: catcall_display_t, catcall_touch_t, purr_win.h
  purr_sdk_claw.h         .claw: all above + purr_kernel.h + purr_module.h
  catcall_display.h       copied from source/kernel/catcalls/
  catcall_touch.h
  catcall_input.h
  catcall_radio.h
  catcall_gps.h
  catcall_ui.h            (new in v0.12.0)
  catcalls.h
  purr_win.h              (new in v0.12.0)
  purr_module.h
  purr_kernel.h           (.claw only)
```

Use `purr_sdk.h` in app code — it selects the right tier based on the compile-time define:
- `-DPURR_TIER_PAWS` → includes `purr_sdk_paws.h`
- `-DPURR_TIER_CLAW` → includes `purr_sdk_claw.h`

### Output structure

```
cattobaked/apps/
  settings.claw
  settings.claw.meta.json
  about.claw
  about.claw.meta.json
  terminal.claw
  terminal.claw.meta.json
  fileman.claw
  fileman.claw.meta.json
  calculator.paws
  calculator.paws.meta.json
  magicmac.claw
  magicmac.claw.meta.json
```

---

## Full Build Flow

```
catstrap sdk install             # one-time: set up SDK headers

modulestrap build all            # register all modules + drivers
catstrap build all               # register all apps

purrstrap build tdeck_plus       # build full image for T-Deck Plus
purrstrap flash tdeck_plus       # build + flash
```

purrstrap runs modulestrap and catstrap automatically as part of `purrstrap build`, so you only need to run them separately for incremental development.

---

## Environment Requirements

| Tool | Required for | Install |
|------|-------------|---------|
| Python 3.8+ | all three tools | system |
| ESP-IDF 5.x | kernel spine build | idf.py |
| esptool.py | flash + merge | included with IDF |
| spiffsgen.py | SPIFFS image | included with IDF |

Set `IDF_PATH` to your ESP-IDF root before building the kernel spine. All three tools gracefully degrade without IDF — they will register blobs and produce SPIFFS images but skip the IDF-dependent steps.
