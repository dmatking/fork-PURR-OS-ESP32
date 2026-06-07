# PURR OS Post-Build TODO

**Status:** ✅ **Pure IDF migration stable** — cyd_boot boots verified on CYD + JC3248W535. Removed ~300 KB Arduino overhead.
**cyd_boot:** 305 KB (factory partition, 71% free) — bootloader UI + recovery mode
**cyd OS:** ~700-800 KB (ota_0, ~50-55% free) — full OS pending build completion
**Architecture:** MiniWin as window manager + UI skins (BlackBerry/Explorer/ClassicMac) planned for Phase 3

---

## Phase 1: First Successful Build & Hardware Test

- [x] Complete cyd_boot build ✅
  - [x] Fixed missing `app_main()` entry point for ESP-IDF FreeRTOS
  - [x] Created partition_manager_stubs.cpp to work around esp_vfs_fat.h include issue
  - [x] Verified full build completes successfully (305 KB binary)
  
- [ ] Flash cyd_boot to CYD hardware
  ```powershell
  idf.py -p COM3 flash monitor
  ```

- [ ] Verify boot sequence:
  - [ ] LVGL initializes (display lights up)
  - [ ] BlackBerry UI homescreen renders
  - [ ] Status bar shows "PURR OS" or device name
  - [ ] Touch input responds to taps
  - [ ] LED heartbeat blinks blue on CYD (if RGB LED present)

**Note on partition_manager:** Currently using stubs (partition_manager_stubs.cpp) that provide OTA slot/launch/delete functionality but skip SD card support. Full SD card implementation (dump_to_sd, install, pm_init) deferred to Phase 6 — resolves `esp_vfs_fat.h` include path issue.

---

## Phase 2: Core Features Validation

- [ ] Test BlackBerry UI navigation:
  - [ ] Tap app grid cells (should highlight)
  - [ ] Swipe to switch tabs (Frequent/All/Favorites)
  - [ ] Open dock (4 quick-access buttons)
  
- [ ] Test app launcher:
  - [ ] Create simple test .paws Lua app
  - [ ] Launch from app grid
  - [ ] Verify Lua interpreter initializes
  - [ ] Return to homescreen when app closes

- [ ] Test system functions:
  - [ ] WiFi enable/disable (status bar updates)
  - [ ] Bluetooth enable/disable
  - [ ] Check time display updates
  
- [ ] Verify touch accuracy:
  - [ ] CST816S coordinates map 1:1 to screen
  - [ ] Multi-tap works (app launcher)

- [ ] Test backlight:
  - [ ] Brightness slider works (brightness controls via LEDC)

---

## Phase 3: MiniWin Skin Architecture

- [ ] Design MiniWin integration:
  - [ ] Create `miniwin_shell.cpp` wrapper
    - Initialize MiniWin window manager in LVGL parent
    - Register as `PURR_SHELL_MINIWIN` in purr_wm
    - Route touch/key events to MiniWin
  
- [ ] Create skin theme modules:
  - [ ] `miniwin_skin_blackberry.cpp` — BB OS 6 aesthetic
    - BB color palette (dark blue/gray)
    - 4-column app grid styling
    - Slide-up drawer animation
    
  - [ ] `miniwin_skin_explorer.cpp` — Windows CE aesthetic
    - CE taskbar look
    - Desktop + shortcut strip
    - Window chrome
    
  - [ ] `miniwin_skin_classicmac.cpp` — Mac System 7/8 aesthetic
    - Platinum theme colors
    - Menu bars + window controls
    - Finder-style layout

- [ ] Implement skin switching:
  - [ ] Add to settings menu
  - [ ] Persist choice in NVS
  - [ ] No app relaunch required (visual-only change)

- [ ] Test each skin on hardware:
  - [ ] Renders without glitches
  - [ ] Touch input works in all skins
  - [ ] Skin switch animation is smooth

---

## Phase 3b: Heltec SMOL LVGL UI

- [ ] Create minimal LVGL shell for Heltec OLED (128×64):
  - [ ] `heltec_ui.cpp` — monochrome-optimized homescreen
    - Status line (battery, signal, time)
    - 2-row app launcher (fit ~4-6 apps)
    - Simple menu navigation
    - Low-memory optimized (smaller font, fewer colors)
  
  - [ ] `heltec_ui.h` — shell interface
    - Register as `PURR_SHELL_HELTEC` in purr_wm
    - Route button/encoder input to app selection
  
  - [ ] Optimize for monochrome:
    - Use dithering or B&W theme
    - Reduce animation overhead
    - Minimize draw buffer size (target: ~1KB)
  
  - [ ] Test on hardware:
    - Verify text readability at 128×64
    - Check button input mapping
    - Measure frame rate (target: 30+ FPS)

---

## Phase 4: Display & Touch Improvements

- [ ] Review fork for display enhancements:
  - [ ] Check color calibration fixes
  - [ ] Check refresh rate optimizations
  - [ ] Check touch coordinate improvements
  
- [ ] Integrate into display_ili9341.cpp:
  - [ ] Apply calibration if found
  - [ ] Test on hardware
  - [ ] Measure color accuracy improvement

---

## Phase 5: WIP Device Support

- [ ] Migrate remaining touch drivers to pure IDF:
  - [ ] **MXT336T** (I2C Atmel touch)
    - Replace Arduino Wire with `i2c_master_transmit_receive`
    - Test on hardware if available
    
  - [ ] **XPT2046** (SPI touch — CYD variant)
    - Already mostly done, verify SPI transfers work
    - Test pressure sensitivity
    
- [x] JC3248W535 (ESP32-S3, 3.5") target ✅
  - [x] Hardware boots confirmed — bootloader UI works
  - [x] ST7796 display driver (pure IDF)
  - [x] GT911 touch driver (pure IDF)
  - [ ] Add UI shell (BlackBerry or MiniWin-based) — currently uses cyd_boot fallback
  
- [ ] Verify T-Deck Plus (trackball + keys):
  - [ ] Key input maps to generic_key_t
  - [ ] Trackball encoder works with MiniWin
  - [ ] Test menu navigation with trackball

---

## Phase 6: Cleanup & Optimization

- [ ] Fix compiler warnings:
  - [ ] "Serial defined but not used" in purr_idf_compat.h
    - Use `__attribute__((unused))` or conditional instantiation
  
- [ ] Re-enable partition_manager:
  - [ ] Fix `esp_vfs_fat.h` header path
    - Check if needs `<esp_vfs/esp_vfs_fat.h>`
    - Or add fatfs include path to CMakeLists
  - [ ] Test SD card enumeration
  - [ ] Test firmware backup/restore
  
- [ ] Full feature build with all modules:
  - [ ] WiFi manager
  - [ ] Bluetooth manager
  - [ ] LoRa (heltec/tdeck only)
  - [ ] Meshtastic daemon (if ported)
  
- [ ] Measure final size:
  - [ ] Binary size (firmware.elf)
  - [ ] RAM usage (internal SRAM occupied)
  - [ ] PSRAM usage (if applicable)
  - [ ] Document in PURR_OS_docs/

---

## Phase 7: Meshtastic Integration (Optional)

- [ ] Check ported Meshtastic daemon status
- [ ] Verify RadioLib LoRa operations work
- [ ] Move daemon data/buffers to PSRAM:
  - Mesh routing tables → PSRAM
  - Packet queues → PSRAM
  - Keep ISR handlers in internal SRAM
  
- [ ] Test WiFi bridge (MQTT relay)
- [ ] Verify internal heap available for apps (~260KB target)

---

## Post-Completion

- [ ] **DELETE THIS FILE**: `rm PURR_TODO.md`
- [ ] Create CHANGELOG for v0.8.0 (Arduino → pure IDF migration)
- [ ] Update README with:
  - Build instructions (SDK.ps1)
  - Hardware support matrix (CYD, T-Deck Plus, JC3248W535)
  - Memory constraints
  - App development guide
- [ ] Tag release: `git tag v0.8.0-idf`
