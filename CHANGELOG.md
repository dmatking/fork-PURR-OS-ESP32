# CHANGELOG

> **Note:** This changelog covers PURR OS v0.12.0 and later — the modular architecture era.
> History up to v0.11.0 is preserved in [archive/CHANGELOG_0.11.md](archive/CHANGELOG_0.11.md).
> v0.11.0 was the final release of the monolithic build system. Everything from here is new.

---

## v0.12.0 — 2026-06-13

### Summary
Complete ground-up architecture redesign and the first full release of the PURR OS modular era.
Every driver, UI framework, and app is an isolated precompiled module. The kernel spine has zero
hardware knowledge — it just loads modules and hands off. KITT v0.9.0 ships alongside with the
same catcall-based kernel interface translation toolkit.

This release also introduces the **Unified UI API** (`purr_win.h`), a base set of five system apps
that work identically on all UI backends, and per-device `[radio]` and `[apps]` configuration so
builds are explicitly declared rather than assembled at runtime.

### New in v0.12.0

#### catcall_ui_t — Unified UI catcall
- New sixth catcall: `catcall_ui_t` registered via `purr_kernel_register_ui()`, accessed via `purr_kernel_ui()`
- Covers windows, labels, buttons, textareas, layout containers, on-screen keyboard
- `source/kernel/catcalls/catcall_ui.h` — full struct definition
- `source/kernel/catcalls/purr_win.h` — thin inline dispatch header for apps; all `purr_win_*()` helpers null-check before calling through the registered backend

#### KittenUI LVGL backend
- `source/modules/kittenui/kittenui_win.c` — implements `catcall_ui_t` using LVGL 8.x
- Handle pool: `s_wins[16]` / `s_wids[128]` → `lv_obj_t*`
- Button/textarea callbacks via `cb_ctx_t` trampoline structs
- LVGL keyboard attached to textarea on `kb_show`
- Called from `kittenui_module.c` init via `kittenui_win_register()`

#### MiniWin backend
- `source/modules/miniwin/miniwin_win.c` — implements `catcall_ui_t` using MiniWin WM
- Handle pool: `win_slot_t s_wins[16]` / `wid_slot_t s_wids[128]`
- Vertical cursor stacking for label/button layout
- `kb_show` is a no-op — physical keyboard handled via `catcall_input` automatically
- Called from `miniwin_module.c` init via `miniwin_win_register()`

#### Five built-in system apps
All apps use `purr_win.h` exclusively — zero direct LVGL or MiniWin calls.
They run identically on KittenUI and MiniWin without any source changes.

| App | Tier | Description |
|-----|------|-------------|
| `settings` | `.claw` | Theme switcher (WCE/Luna/Dark, persisted to NVS "kittenui" namespace), display brightness via `catcall_display->set_brightness`, SD card status, system reboot |
| `about` | `.claw` | PURR OS + KITT version, chip model/revision/cores, flash size, free RAM, uptime (live-updating every 5 s via background FreeRTOS task), all active catcall driver names |
| `terminal` | `.claw` | Interactive shell: `ls [path]`, `cat <path>`, `echo <text>`, `modules`, `mem`, `uptime`, `clear`, `reboot`, `help`; 2 KB output scroll buffer |
| `fileman` | `.claw` | Browse SPIFFS + SD card; Prev/Next cursor selection; Open enters directories or previews text files; binary-safe (non-printable bytes replaced with `.`) |
| `calculator` | `.paws` | Basic arithmetic: `+`, `-`, `*`, `/`, decimal point, ERR:DIV0 guard; 5-row button layout via `purr_win_row()` |

#### Device configs — [radio] and [apps] sections
Every `device.pcat` now has:
- `[radio]` — declares `wifi`, `bt`, `lora` capabilities; purrstrap emits `CONFIG_PURR_WIFI`, `CONFIG_PURR_BT`, `CONFIG_PURR_LORA`, `CONFIG_PURR_LORA_DRIVER` into the glue layer
- `[apps]` — per-app `true/false` flags; controls which apps are baked into the SPIFFS flash image
- Medium/large-screen devices (cyd*, tdeck*, jc3248w535) get all five apps bundled by default
- Small-screen devices (heltec, waveshare169) ship without GUI apps

#### purrstrap — full pipeline wired
- `_generate_glue()` now emits radio flags and `/flash/apps` + `/sdcard/apps` path constants
- `build_flash_image()` now calls catstrap automatically (was manual step before)
- `_find_purr_blob()` handles `apps/<name>` slugs: resolves `.claw`/`.paws`/`.meow` output files and `.meta.json` registrations
- App blob staging into `spiffs_staging/apps/` alongside modules and drivers

#### New GPS driver
- `source/drivers/gps/generic_nmea/generic_nmea.c` — full UART NMEA 0183 parser
- Background FreeRTOS task reads UART line by line; parses `$GPRMC` (lat/lon/speed/validity) and `$GPGGA` (satellites/hdop/altitude)
- Mutex-protected `gps_fix_t`; thread-safe `get_fix()` reads
- PURR_PRIORITY_OPTIONAL — kernel does not panic if GPS is absent

#### New OLED UI module
- `source/modules/oled_ui/oled_ui_module.c` — text-mode UI for SSD1306 128x64
- Embedded 6x8 bitmap font (95 printable ASCII chars)
- Ring-buffer log display (5 visible lines); `oled_ui_log(const char *)` public API for other modules
- Row 0 = title bar, row 1 = status, row 2 = separator, rows 3-7 = scrolling log
- Redraws every 500 ms via FreeRTOS task

#### app_manager — fully wired
- `launch_meow()` — looks up `lua_runtime` module via `purr_kernel_get_module()`, stores pending path, spawns `meow_task()`
- `launch_native()` — looks up app name via `purr_kernel_get_module()`; reports "not pre-linked: recompile firmware" if absent; spawns `native_task()` (16 KB stack for .claw, 8 KB for .paws)
- `app_manager_stop()` — calls `mod->deinit()`, waits up to 2 s via semaphore, force-deletes task via `vTaskDelete()` if still alive
- `app_manager_open_launcher()` — logs app list with name/tier/state
- `app_task_ctx_t` struct tracks task handle + done semaphore per app slot

#### catstrap — IDF component model
- `build_app()` now generates IDF component `CMakeLists.txt` fragments with correct include paths to `source/kernel/` headers
- Writes `.meta.json` with status "registered" (was "placeholder" before)
- .claw apps get `source/kernel/core/` + `source/kernel/catcalls/` in their include dirs

#### Documentation refresh (docs/)
- `02_Catcalls.md` — added `catcall_ui_t` section with full struct, functions, backend table, app usage guide; added Glue Layer section documenting `CONFIG_PURR_WIFI`/`BT`/`LORA` defines; added per-catcall driver tables
- `04_Devices.md` — added all 8 devices (was 4); added `[radio]`, `[apps]`, `[flash]` format; added screen size classification table; expanded pin reference with LoRa busy pin, MADCTL override, SD keys
- `06_Apps.md` — rewrote with `purr_win.h` API reference, full function signatures for all widget types; added built-in system apps table; updated .meow kitt.* section with `radio_rssi()`, GPS fix fields
- `07_Build_Tools.md` — complete rewrite; documents full purrstrap pipeline (7-step sequence), modulestrap IDF component model, catstrap SDK headers including `catcall_ui.h` and `purr_win.h`
- `12_AppAPI.md` — added settings/about/fileman to built-in apps table; added file manager layout diagram

### Bug fixes
- `oled_ui_module.c` — removed duplicate kernel header includes (double `../../kernel/` and `../../../source/kernel/` paths would fail compilation)
- `modulestrap` — removed broken per-module IDF mini-project invocation (ESP-IDF cannot build isolated component mini-projects); replaced with component registration model
- `cyd_s024c` — backlight pin was GPIO 21 in old config; corrected to GPIO 27 (verified)
- `cyd_s028r` — missing MADCTL=0x40 portrait flip; added `display_madctl` pin key

### Breaking changes from v0.11.0
- Old `CoreOS/`, `drivers/`, `ui/`, `devices/`, `SDK/`, `CattoHID/`, `Userland/`, `sdcard_apps/` are archived to `PURR-OS-0.11/` — not built by default
- `purrstrap.py` (monolithic CLI) replaced by three separate tools: `purrstrap/`, `modulestrap/`, `catstrap/`
- Build output moves from `baked/<device>/` to `cattobaked/<device|modules|drivers|apps>/`

### Versions
- PURR OS: v0.12.0
- KITT: v0.9.0
- `.purr` ABI version: 1
- Catcall API version: 1 (+ catcall_ui added as slot 6)

---

*v0.11.0 and earlier: see [archive/CHANGELOG_0.11.md](archive/CHANGELOG_0.11.md)*
