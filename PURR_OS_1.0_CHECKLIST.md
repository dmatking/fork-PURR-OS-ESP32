# PURR OS v1.0 — Release Preparation Checklist

> v0.13.0 and v0.14.0 are the final interim releases before 1.0.
> This checklist covers everything that must be confirmed, fixed, or documented
> before the 1.0 tag is cut. Work items are grouped by area. Check off each item
> as it is completed.

---

## 1. Hardware / Device Validation

All catcalls must be confirmed working on every supported device before 1.0.

### T-Deck Plus (`tdeck_plus` kernel — IDF path, canonical as of the consolidation pass below)
- [ ] Display — ST7789 320×240, correct colors, no inversion
- [ ] Touch — GT911 responds and stays awake (power-save keepalive confirmed)
- [ ] Trackball — all 4 directions + click register correctly
- [ ] Keyboard — BBQ20 all keys respond, no bus lockout
- [ ] LoRa SX1276 — send/receive/RSSI confirmed
- [ ] GPS — NMEA fix acquired and surfaced through `catcall_gps_t`
- [ ] SD card — mount, read, write confirmed
- [ ] Cardstack — responds to touch/trackball/keyboard correctly, hosts app windows cleanly

### T-Deck Plus IDF-kernel migration gate (must pass before `kernel_tdeck_plus_arduino` is archived)

`kernel_tdeck_plus` (IDF i2c_master path) received a GT911 fix in commit `322159c6`
that makes it functionally equivalent to `kernel_tdeck_plus_arduino` (the Wire-based
workaround kernel, now deprecated — see `docs/13_Kernels.md`). Before deleting the
Arduino kernel, confirm on real hardware:

- [ ] Flash `tdeck_plus` (IDF kernel) to a physical unit via `purrstrap flash tdeck_plus -p <port> --erase`
- [ ] No `ESP_ERR_INVALID_STATE` (the original IDF 5.3 regression symptom) appears in the serial log during GT911 init
- [ ] Tap all 4 screen corners + center — coordinates are correct and non-mirrored (validates the byte-offset/signed-coordinate fix)
- [ ] Power-cycle 10+ times consecutively — GT911 I2C address (0x5D) latches reliably every time, not just on a fresh boot
- [ ] Trackball, BBQ20 keyboard, SD, LoRa (SX1276), and GPS all match current `tdeck_plus_arduino` behavior
- [ ] Extended soak test (hours, not minutes) — no I2C bus lockups or touch-driver hangs under sustained use
- [ ] WiFi STA and battery-status reporting confirmed working on the IDF kernel, or explicitly scoped out if not yet ported (the IDF kernel does not currently bring these up the way the Arduino kernel does — see `docs/13_Kernels.md`)
- [ ] Independent sign-off — someone other than the original author flashes and confirms separately

Once every item above is checked, proceed with archiving `kernel_tdeck_plus_arduino`
to `archive/` per the consolidation plan.

### T-Deck (`tdeck` kernel)
- [ ] Display, touch, trackball, keyboard
- [ ] LoRa SX1262 send/receive
- [ ] SD card

### CYD variants (`cyd`, `cyd_s024c`, `cyd_s028r`)
- [ ] Display (ILI9341) correct orientation per variant
- [ ] Touch (XPT2046 resistive) calibration
- [ ] SD card

### JC3248W535 (`jc3248w535`)
- [ ] Display (AXS15231B 480×320 QSPI)
- [ ] Touch

### Heltec (`heltec`)
- [ ] OLED SSD1306 + oled_ui module
- [ ] LoRa SX1262

### Waveshare 1.69" (`waveshare169`)
- [ ] Display (ST7789 240×280)
- [ ] Touch (if wired)

---

## 2. Kernel

- [ ] Generic core kernel (`source/kernel/core/`) boots cleanly on all non-specialized devices
- [ ] Specialized kernel selection in CMake works correctly for all `kernel_<device>` directories
- [ ] `kernel_tdeck_plus_arduino` — all baked-in drivers fully functional (see §1)
- [ ] `kernel_tdeck_plus_test` — test mode kernel boots and shows all input events (confirm keyboard works after BBQ20 fix)
- [ ] Arduino kernels (`_arduino`, `_test`) — `Wire.setTimeOut(50)` confirmed preventing bus hangs
- [ ] GT911 status register 0x814E always cleared after every read with buffer-ready bit set
- [ ] GT911 power-save keepalive (write 0x00 to 0x8040 every 2 s) confirmed stable over long sessions
- [ ] Serial console "Writing to serial is timing out" bug resolved in non-Arduino kernels
- [ ] `purr_kernel_panic()` triggers clean reboot with log message on all kernels
- [ ] BOARD_POWERON (GPIO 10 on T-Deck Plus) boot sequence documented and reliable

---

## 3. Catcall API

- [ ] All six catcalls have stable struct layouts — no field additions planned before 1.0
- [ ] `CATCALL_*_VERSION` constants are correct in all headers
- [ ] `catcall_display_t` — `push_pixels`, `fill_rect`, `set_brightness` tested on all display drivers
- [ ] `catcall_touch_t` — `read_point`, `is_pressed` work correctly through all touch drivers
- [ ] `catcall_input_t` — `poll_event` works for BBQ20 and trackball
- [ ] `catcall_radio_t` — `send`, `receive`, `rssi`, `snr` tested on SX1276 and SX1262
- [ ] `catcall_gps_t` — `get_fix` returns valid struct on T-Deck Plus
- [ ] `catcall_ui_t` — `create_window`, `add_label`, `add_button`, `show`, `destroy` work on both backends
- [ ] Kernel accessors (`purr_kernel_display()`, etc.) return NULL cleanly when driver not registered
- [ ] No catcall header has any IDF-specific types in its public interface (must be portable)

---

## 4. Driver Layer

### Display
- [ ] `st7789` — correct on T-Deck/T-Deck Plus/Waveshare
- [ ] `ili9341` — correct on all CYD variants
- [ ] `axs15231b` — correct on JC3248W535 (QSPI)
- [ ] `ssd1306` — correct on Heltec OLED
- [ ] All display drivers call hardware init from `drv_init()` (regression fixed in v0.12.1 — confirm still correct)

### Touch
- [ ] `gt911` — IDF i2c_master path (non-Arduino kernels): document as known-broken on IDF 5.3, workaround is Arduino kernel
- [ ] `gt911` — Wire path (Arduino kernels): fully working
- [ ] `xpt2046` — resistive touch on CYD devices
- [ ] `cst816s` — if used on any device, confirm

### Input
- [ ] `trackball` — GPIO ISR, all directions
- [ ] `bbq20` — raw Wire read (no write preamble), clean on bus

### Radio
- [ ] `sx1276` — LoRa send/receive/RSSI on T-Deck Plus
- [ ] `sx1262` — LoRa send/receive/RSSI on T-Deck, Heltec

### GPS
- [ ] `generic_nmea` — UART parse, valid fix struct returned

### Driver ABI
- [ ] All driver `CMakeLists.txt` list correct REQUIRES (no missing IDF components)
- [ ] All `.purr` driver blobs produce a valid `catcall_*_t` and call `purr_kernel_register_*()`
- [ ] `user_drivers/` auto-scan path works end-to-end (drop a driver in, bake, it loads)
- [ ] Every specialized kernel consumes drivers by calling `<name>_drv_init()`/`<name>_configure()` from `source/drivers/` — no specialized kernel reimplements register-level driver logic from scratch (see `docs/01_Architecture.md` — Specialized Kernels rule). `kernel_tdeck_plus_arduino` is the sole grandfathered exception pending §1's migration gate.

---

## 5. Module System

- [ ] `driver_manager` — scans SPIFFS and SD, loads `.purr` blobs in correct order
- [ ] `app_manager` — scans SPIFFS and SD, launches `.meow`/`.paws`/`.claw` apps
- [ ] `kittenui` — LVGL 8 backend: window create, label, button, text box, show, destroy
- [ ] `miniwin` — MiniWin backend: same surface, touch dispatch (fixed in `hal_touch.c` — pending on-device confirmation on `jc3248w535`, see §11)
- [ ] `oled_ui` — text-mode UI for 128×64 OLED (Heltec)
- [ ] Static module registration (`purr_register_static_modules`) works without SD present
- [ ] `.purr` ABI version check — mismatched ABI version is rejected cleanly with log message

---

## 6. App Layer

### System apps (all medium/large-screen devices)
- [ ] `settings` — theme, brightness, SD status, reboot all functional
- [ ] `about` — OS version, KITT version, chip info, free RAM, uptime correct
- [ ] `terminal` — `ls`, `cat`, `echo`, `modules`, `mem`, `uptime`, `reboot` all work
- [ ] `fileman` — SPIFFS + SD browse, text preview
- [ ] `calculator` — basic arithmetic, decimal support

### Exclusives
- [ ] `magicmac` — current status documented; either working or explicitly deferred to post-1.0
- [ ] `magidos` — same

### `purr_win.h` unified API
- [ ] All system apps compile and run against both KittenUI and MiniWin backends without changes
- [ ] `purr_win_create`, `purr_win_label`, `purr_win_button`, `purr_win_input`, `purr_win_show`, `purr_win_destroy` all work on both backends

---

## 7. Build Tools

### purrstrap
- [ ] `build`, `flash`, `clean`, `list`, `status`, `doctor` all work
- [ ] `--erase` flag works correctly (full chip erase before flash)
- [ ] Per-device sdkconfig chaining works for all 10 device targets (including `_arduino` and `_test`)
- [ ] `purrstrap_ui.py` — GUI launcher functional if shipped with 1.0
- [ ] Build output paths are consistent (`cattobaked/<device>/`)

### modulestrap
- [ ] `build all`, `build <name>`, `list`, `clean` work
- [ ] All 5 system modules build without warnings
- [ ] All driver blobs build without warnings

### catstrap
- [ ] `build all`, `build <name>`, `validate`, `sdk install`, `sdk info`, `list`, `clean` work
- [ ] SDK headers (`catstrap/sdk/include/`) are generated correctly
- [ ] `.meow` Lua scripts validate cleanly

### purr.py / purr.sh / purr.ps1
- [ ] Cross-platform launcher scripts are consistent and documented
- [ ] `purr.py` is the canonical entrypoint for the unified toolchain CLI

---

## 8. Documentation

### README.md
- [ ] Version table updated to v0.13.0 (or latest before 1.0)
- [ ] Architecture diagram still accurate (update for specialized kernels)
- [ ] Supported devices table accurate (add `tdeck_plus_arduino`, `tdeck_plus_test` or clarify they are dev targets)
- [ ] Build tool command reference matches actual purrstrap/modulestrap/catstrap behavior
- [ ] Repo layout section reflects current directory structure
- [ ] Versions table reflects actual PURR OS / KITT / ABI versions

### docs/ files
- [ ] `00_Overview.md` — device list current, concepts current
- [ ] `01_Architecture.md` — layer model updated to include specialized kernels (kernel_arduino, kernel_tdp, etc.)
- [ ] `02_Catcalls.md` — all 6 catcalls documented with current struct fields
- [ ] `03_Modules.md` — all 5 modules documented; MiniWin status note if still broken
- [ ] `04_Devices.md` — all devices present including T-Deck Plus; pin tables complete
- [ ] `05_Drivers.md` — all drivers documented; GT911/IDF 5.3 known issue noted
- [ ] `06_Apps.md` — system apps + exclusives current
- [ ] `07_Build_Tools.md` — purrstrap/modulestrap/catstrap current (`09_BuildTools.md` duplicate already deleted in v0.13.0 — nothing further to do here)
- [ ] `08_Exclusives.md` — MagicMac/MagiDOS status is accurate
- [ ] `10_ModuleLoading.md` — module loading pipeline current
- [ ] `11_KittenUI.md` — LVGL 8 API current; XP desktop shell status
- [ ] `12_AppAPI.md` — `purr_win.h` API reference complete and accurate

### CHANGELOG.md
- [ ] v0.13.0 entry written (specialized kernels, Arduino kernel, GT911 fix, test mode)
- [ ] v0.14.0 entry written before 1.0 tag
- [ ] 1.0.0 entry written at tag time

### PURR_OS_0.13.0.md
- [ ] Architectural decisions captured; mark which items are done vs deferred

---

## 9. Code Quality

- [ ] No `TODO` / `FIXME` / `HACK` comments left in any file that will ship in 1.0
- [ ] No dead kernel directories (e.g. stale test variants not in any device.pcat)
- [ ] `CoreOS/sdkconfig.old` — delete or gitignore
- [ ] `archive/` — confirm nothing in there is needed by the active build
- [ ] `PURR-OS-0.11/` — confirm it is reference-only and excluded from builds
- [ ] All `.bin` files in `releases/` and `archive/releases_legacy/` are correct and not accidentally modified
- [ ] `purr.ps1` / `purr.py` / `purr.sh` — consistent feature parity across platforms

---

## 10. Repo Hygiene

- [ ] `.gitignore` covers all build output (`CoreOS/build_*/`, `cattobaked/`, `*.bin` in wrong places)
- [ ] No secrets, keys, or credentials anywhere in tree
- [ ] All submodule or managed_component pins are explicit versions (no `@latest`)
- [ ] `idf_component.yml` — `espressif/arduino-esp32 >= 3.0.0` — confirm this resolves deterministically
- [ ] Branch is clean — no accidental staged files
- [ ] All commits on `main` have coherent messages (squash any WIP commits before 1.0 tag)
- [ ] GitHub releases for v0.12.0, v0.12.1 are accurate; v0.13.0 and v0.14.0 releases drafted before 1.0 tag

---

## 11. Known Issues to Resolve Before 1.0

| Issue | Status | Target |
|-------|--------|--------|
| MiniWin does not respond to touch | Fixed in code (stale `>>6` digitiser-scale hack in `hal_touch.c` removed — was crushing already-pixel-space coordinates toward zero) — **needs on-device confirmation on `jc3248w535`** | v0.13.1 |
| Serial console timeout on non-Arduino kernels | Open | v0.13.0 |
| GT911 over IDF i2c_master (ESP_ERR_INVALID_STATE) | Workaround: Arduino kernel | Document in v0.13.0, fix or remove IDF path in 1.0 |
| BBQ20 keyboard in test kernel | Fix flashed — needs confirm | v0.13.0 |
| KittenUI XP desktop shell — touch dispatch | Partial | v0.13.0 |
| MagicMac / MagiDOS rewrite | In progress | v0.14.0 or post-1.0 |

---

## 12. 1.0 Tag Criteria (all must be ✅)

- [ ] Every device in the supported table boots to a working UI
- [ ] All six catcalls functional on all devices that have the hardware
- [ ] `purr_win.h` API works on both KittenUI and MiniWin
- [ ] All system apps functional on all medium/large-screen devices
- [ ] `purrstrap build/flash/clean` works for all 8 production targets
- [ ] README and all docs/ files are accurate and complete
- [ ] CHANGELOG has entries for v0.13.0, v0.14.0, and v1.0.0
- [ ] Repo is clean, no WIP commits, no accidental binaries staged
- [ ] GitHub release created with merged firmware for all 8 devices

---

*Last updated: v0.12.1 / 2026-06-15 — tracking toward v1.0.0*
