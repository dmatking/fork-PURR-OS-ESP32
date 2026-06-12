# CHANGELOG

## v0.11.0 — 2026-06-12 (current)

### Version bump
- PURR OS: v0.10.1 → v0.11.0
- KITT: v0.6.9 → v0.8.0

### New vs v0.10.0
- **purrstrap.py** — pmbootstrap-style CLI replaces `SDK/sdk_core.py`; subcommands: `init`, `status`, `list`, `build`, `flash`, `install`, `monitor`, `clean`, `bake`, `release`, `scan`, `doctor`
- **Doctor command** — 15-point environment + repo health check (Python, ESP-IDF version, esptool, pyserial, component dirs, device overlays, serial ports)
- **Repo restructure** — `drivers/` and `ui/` at repo root replace `CoreOS/components/`; all CMakeLists paths updated
- **T-Deck Plus** — full keyboard + trackball input with 5-second cursor auto-hide, mouse cursor rendering
- **MagiDOS CGA layer** — `magidos_cga.cpp/.h` and `magidos_filepicker.cpp` added
- **Device compile-time config** — `device_config_default()` fully baked in for all targets; no SPIFFS/device.json dependency
- **GT911 touch fix** — I2C bus conflict resolved; NULL guard prevents touch spam brownout
- **GPS boot fix** — probe timeout 1500→800ms; total GPS wait 2000→500ms
- **SD card fix** — 250ms GPIO 10 power stabilization delay on T-Deck Plus
- **IDF sdmmc patch** — `ESP_ERR_INVALID_SIZE` allowed in SPI mode (`sdmmc_io.c`)
- **4 new docs** — Overview, Quick Start, Architecture, Devices; all old docs archived
- **baked/ output** — `purrstrap bake` produces per-device folder with binaries + `flash.sh` + `FLASH_GUIDE.md`
- **9 targets all building** — heltec, tembed_cc1101, cyd_s028r, cyd_s024c, cyd_boot, tdeck, tdeck_plus, jc3248w535, waveshare169

---

## v0.10.1 — 2026-06-11

### Repo restructure
- `CoreOS/components/drv_*` → `drivers/` at repo root
- `CoreOS/components/lib_miniwin` + `lib_tftespi` → `ui/`
- `Shells/purr_wm` → `ui/purr_wm`
- Device folders renamed to match target names: `jc3248` → `jc3248w535`, `waveshare` → `waveshare169`
- Added missing device stub folders: `cyd_s024c`, `heltec`, `tembed_cc1101`, `tdeck`
- All CMakeLists paths updated for new layout
- `lib_miniwin/CMakeLists.txt` path fixes + added explicit `cyd_s024c` HAL mapping

### purrstrap.py (new)
- Replaces `SDK/sdk_core.py` — pmbootstrap-style CLI
- Subcommands: `init`, `status`, `list`, `build`, `flash`, `install`, `monitor`, `clean`, `bake`, `release`, `scan`
- `baked/<device>/` output with `flash.sh` + `FLASH_GUIDE.md` per device
- `.purrstrap` config at repo root (gitignored)
- Release sets: `all`, `miniwin`, `s3`, `cyd`

### Runtime fixes (tdeck_plus)
- Fixed I2C bus conflict: GT911 sys_drv disabled on tdeck_plus; `hal_touch.cpp` owns the bus
- Fixed touch spam → brownout: added NULL guard in `mw_hal_touch_get_state()`
- Fixed ADC crash: `BATT_ADC_PIN = -1` for tdeck_plus (no exposed battery ADC)
- Fixed GPS blocking 5s boot: `GPS_INIT_WAIT_MS` 2000→500ms, probe timeouts 1500→800ms
- Fixed SD card power: 250ms stabilization delay after GPIO 10 HIGH
- Applied IDF patch: `sdmmc_io.c` allows `ESP_ERR_INVALID_SIZE` in SPI mode

### Device config
- `device_config_default()` fully baked in at compile time for all targets — no `device.json` / SPIFFS dependency
- `kitt.cpp` always calls `device_config_default()`, never loads JSON
- `PURR_TARGET_*` defines added to `CoreOS/main/CMakeLists.txt` for all devices

### Desktop / UI
- Removed legacy `desktop_icons_register_defaults()` from tdeck_plus `purr_app.cpp`
- Removed identity matrix calibration pre-seed — MiniWin 3-point calibration runs on first boot
- Drivers app USER tab shows "No SD card / No user drivers" when `!pm_sd_available()`

### Repo cleanup
- Archived: `SDK/` legacy scripts, `WIP/` shell experiments, old `releases/` binaries
- Removed root-level scratch files (`CMakeLists.txt`, `main.cpp`, `purr_api.h`)
- Updated `.gitignore`: `baked/`, `tmp/`, `.purrstrap`

---

## v0.10.0 — 2026-05-xx

- MiniWin WM: keyboard + trackball support across system
- Trackball mouse cursor with 5s auto-hide
- Cross-device build fixes (ILI9341/pm guards, GT911/ST7796, misleading-indentation)
- Increased blue panic memory threshold to 50KB

---

## v0.9.6 — 2026-04-xx

- MagiDOS 8086 DOS emulator (ESP32-S3 + 8MB PSRAM)
- MagicMac Mac Plus emulator stub
- SPIFFS partition offset corrected to 0xe00000
- CYD white display fix on boot

---

*Older history archived in [docs/archive/](docs/archive/).*
