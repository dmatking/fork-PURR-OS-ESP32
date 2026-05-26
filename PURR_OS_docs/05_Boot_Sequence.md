# PURR OS Boot Sequence — Detailed Specification

## Overview

The PURR OS boot sequence is a strict layered chain. Each layer validates the next before handing off. No layer skips ahead. If any critical step fails, the system falls back gracefully rather than hanging — either to a text error screen, MTP recovery mode, or a watchdog-triggered restart. The full chain from power-on to explorer.paw ready takes approximately 3–6 seconds on a Heltec V3 and 4–8 seconds on CattoPad depending on WiFi reconnect and friends scan time.

---

## Full Boot Chain Diagram

```
Power on / Reset
      │
      ▼
┌─────────────────────────────────────────────────────┐
│  ESP-IDF Bootloader (native, ~256KB)                │
│  - Reads partition table                            │
│  - Reads NVS boot flags                             │
│  - Loads /boot/watchdog.purr into memory            │
│  - Transfers execution to watchdog                  │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  watchdog.purr                                      │
│  - Mounts filesystem (SPIFFS / LittleFS)            │
│  - Reads device.json verbose flag                   │
│  - Validates KITT bundle integrity                  │
│  - Spawns KITT                                      │
│  - Starts heartbeat monitor loop                    │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  KITT Kernel (kernel.paw/main.bin)                  │
│  - Reads device.json                                │
│  - Loads display module                             │
│  - Shows boot splash OR verbose log                 │
│  - Checks NVS boot flag                             │
│    ├─ Flag SET   → FLASHER MODE (see below)         │
│    └─ Flag CLEAR → Continue normal boot             │
│  - Loads WiFi, BT, LoRa modules                     │
│  - Loads touch, Pi manager (if present)             │
│  - Scans /friends/ and /apps/                       │
│  - Inits LVGL                                       │
│  - Signals watchdog: KITT ready                     │
│  - Spawns system.paw                                │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  system.paw                                         │
│  - Registers with KITT                              │
│  - Loads LoRa module (if present in device.json)    │
│  - Starts memory monitor                            │
│  - Processes autostart apps from manifests          │
│  - Spawns explorer.paw                              │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  bridge.paw                                         │
│  - Loads keymap from device.json keymap path        │
│  - Registers raw key event reader from KITT         │
│  - Starts key mapping loop                          │
│  - Registers radio handoff broker                   │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│  explorer.paw                                       │
│  - Registers KITT callbacks (tray, popup, notify)   │
│  - Queries KITT for app list and firmware list      │
│  - Builds Start menu                                │
│  - Renders taskbar                                  │
│  - Calls system.paw ready()                         │
│  - OS is now fully operational                      │
└─────────────────────────────────────────────────────┘
```

---

## Stage 1: ESP-IDF Bootloader

Runs before any PURR OS code. Native ESP-IDF, not modifiable.

**Actions:**
- Reads the partition table from flash
- Checks the OTA partition selector in NVS (`otadata`) to determine which app partition to boot
- Loads the selected partition (watchdog.purr) into IRAM
- Transfers execution

**Failure modes:**
- Corrupt partition table → ESP32 ROM bootloader fallback (serial flash only)
- Corrupt watchdog binary → boots from OTA slot 0 fallback if available
- Nothing to boot → ROM serial flasher mode (hold BOOT pin on power-on)

**NVS keys read at this stage:**
```
otadata         → OTA partition selector (managed by ESP-IDF OTA API)
```

---

## Stage 2: watchdog.purr

The boot guardian. Owns restart authority for the entire OS. Runs before KITT initialises. Stays alive for the entire OS session monitoring KITT and system.paw health.

### Startup sequence

```cpp
void watchdog_main(void) {

  // 1. Mount filesystem
  if (!SPIFFS.begin(true)) {
    // Fatal — cannot proceed without FS
    // ESP32 ROM serial output only
    Serial.println("[WDT] FATAL: FS mount failed");
    esp_restart();
  }

  // 2. Read verbose flag from device.json (minimal parse)
  bool verbose = watchdog_read_verbose_flag("/system/kernel.paw/device.json");

  // 3. Validate KITT bundle before spawning
  if (!watchdog_validate_kitt_bundle("/system/kernel.paw/")) {
    Serial.println("[WDT] KITT bundle invalid — trying known-good fallback");
    if (!watchdog_restore_known_good_kitt()) {
      // No fallback available — drop to emergency
      watchdog_trigger_emergency();
      return;
    }
  }

  // 4. Write initial heartbeat baseline to NVS
  nvs_set_u32(nvs_handle, "kitt_hb", 0);
  nvs_set_u32(nvs_handle, "wdt_boot_time", millis());
  nvs_commit(nvs_handle);

  // 5. Spawn KITT
  watchdog_spawn_kitt("/system/kernel.paw/main.bin");

  // 6. Enter monitoring loop (never returns)
  watchdog_monitor_loop();
}
```

### KITT bundle validation

```cpp
bool watchdog_validate_kitt_bundle(const char* bundle_path) {
  // Check manifest exists and parses
  char manifest_path[256];
  snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", bundle_path);
  if (!SPIFFS.exists(manifest_path)) return false;

  // Check main binary exists and is non-zero
  char binary_path[256];
  snprintf(binary_path, sizeof(binary_path), "%s/main.bin", bundle_path);
  if (!SPIFFS.exists(binary_path)) return false;
  File f = SPIFFS.open(binary_path, "r");
  bool ok = (f && f.size() > 0);
  f.close();
  return ok;
}
```

### Known-good KITT fallback

watchdog keeps a backup copy of the last confirmed-working KITT bundle:

```
/system/kernel.paw/          ← Active KITT
/system/kernel.paw.bak/      ← Last known-good KITT (watchdog copies here after confirmed boot)
```

On boot, if the active KITT bundle fails validation, watchdog swaps `.paw.bak` back in and tries again. If the backup also fails, watchdog triggers `emergency.purr`.

### Monitoring loop

```cpp
void watchdog_monitor_loop(void) {
  uint32_t last_known_hb = 0;
  uint32_t kitt_dead_since = 0;

  while (true) {
    delay(1000);  // Check every 1 second

    // Read KITT heartbeat from NVS
    uint32_t current_hb = 0;
    nvs_get_u32(nvs_handle, "kitt_hb", &current_hb);

    if (current_hb == last_known_hb) {
      // Heartbeat hasn't changed
      if (kitt_dead_since == 0) kitt_dead_since = millis();

      uint32_t dead_ms = millis() - kitt_dead_since;

      if (dead_ms >= 3000) {
        Serial.printf("[WDT] KITT heartbeat lost for %ums — restarting KITT\n", dead_ms);
        watchdog_restart_kitt();
        kitt_dead_since = 0;
      }
    } else {
      // Heartbeat updated — KITT is alive
      last_known_hb = current_hb;
      kitt_dead_since = 0;
    }

    // Check system.paw heartbeat (separate NVS key, same logic)
    watchdog_check_system_paw();
  }
}
```

### Confirmed-good boot logic

After KITT has been running stably for 60 seconds without a watchdog restart, watchdog marks the current KITT bundle as known-good and updates the backup:

```cpp
// In watchdog_monitor_loop(), after 60s of clean uptime:
if (clean_uptime_ms >= 60000 && !backup_committed) {
  watchdog_copy_bundle("/system/kernel.paw/", "/system/kernel.paw.bak/");
  backup_committed = true;
  Serial.println("[WDT] KITT confirmed good — backup updated");
}
```

---

## Stage 3: KITT Init (Full Step-by-Step)

See `02_KITT_Kernel_Spec.md` for full API detail. Boot sequence inside `KITT::init()`:

```
Step  1  Open Serial at 115200 baud
Step  2  Mount SPIFFS / LittleFS (already mounted by watchdog — KITT verifies)
Step  3  Read + parse device.json (ArduinoJson)
              → FAIL (0x01): emergency_text("E_JSON_PARSE") + halt
Step  4  Set verbose flag, device name, display config from parsed JSON
Step  5  Load display module
              ILI9488 → display_ili9488_init()
              SSD1306 → display_ssd1306_init()
              → FAIL (0x02): Serial only from here, no display output possible
Step  6  Init LVGL + register display flush callback
              → FAIL (0x07): text_mode fallback, log to serial
Step  7  Verbose branch:
              verbose = true  → start logging all steps to serial + display
              verbose = false → show_boot_splash() from splash file
Step  8  Check NVS boot flag
              nvs_get_u8("boot_flag", &flag)
              flag != 0 → flasher_run() (see Flasher Mode below — never returns)
              flag == 0 → continue normal boot
Step  9  Load WiFi module
              wifi_manager_init()
              If saved SSID in NVS → wifi_connect_async() (non-blocking)
              → FAIL: log warning, continue without WiFi
Step 10  Load BT module
              bt_manager_init()
              Restore paired device list from NVS
              → FAIL: log warning, continue without BT
Step 11  Load LoRa module (if "lora" in device.json radios)
              lora_manager_init(freq_hz, power_dbm)
              → FAIL (0x03): log warning, continue without LoRa
Step 12  Load touch module (if touch != "none" in device.json)
              touch_mxt336t_init()
              → FAIL (0x05): log warning, continue without touch
Step 13  Load Pi manager (if pi_slot = true in device.json)
              pi_manager_init()
              Read handshake GPIO → determine Pi state matrix
              If Pi already active: display_yield_to_pi() immediately
              → FAIL: log warning, continue in ESP32-only mode
Step 14  Load power manager
              power_manager_init()
              Read battery ADC (ADC1 only)
              Set CPU freq to device cpu_max_mhz
Step 15  Scan /friends/ → firmware_scan()
              Find all .purr files
              Cross-reference friends.txt for metadata
              Auto-register any .purr not in friends.txt
              → FAIL (0x06): log warning, Start menu shows no firmware
Step 16  Scan /apps/ → apps_scan()
              Find all .paw bundles with valid manifest.json + main.bin
              Skip invalid bundles (log warning per skip)
Step 17  Register LVGL input device
              Touch → register touch_read_cb as LVGL indev
              Buttons only → register keypad_read_cb as LVGL indev
Step 18  Write KITT_READY = 1 to NVS
Step 19  Send first heartbeat: nvs_set_u32("kitt_hb", millis())
Step 20  Spawn system.paw → app_launch("/system/system.paw/main.bin")
              → FAIL (0x08): text_mode("E_SYSTEM_LAUNCH") + wait for watchdog restart
```

---

## Stage 4: system.paw Init

```cpp
extern "C" int app_create(lv_obj_t* parent) {

  // 1. Register with KITT as the system orchestrator
  kitt_set_system_app_handle(app_get_self_handle());

  // 2. Start memory monitor task (lv_timer, every 500ms)
  lv_timer_create(memory_monitor_cb, 500, NULL);

  // 3. Start NVS heartbeat writer (every 500ms)
  lv_timer_create(system_heartbeat_cb, 500, NULL);

  // 4. Process autostart apps from manifest scans
  //    Check each app's manifest for "autostart": true
  //    Queue them with their autostart_delay_ms values
  autostart_queue_build();
  lv_timer_create(autostart_process_cb, 100, NULL);

  // 5. Spawn bridge.paw
  kitt_app_launch("/system/bridge.paw/main.bin");

  // 6. Spawn explorer.paw
  kitt_app_launch("/system/explorer.paw/main.bin");

  return 0;
}

static void memory_monitor_cb(lv_timer_t* t) {
  memory_stats_t mem;
  kitt_memory_get_stats(&mem);

  uint32_t used_kb = mem.total_ram_kb - mem.free_ram_kb;
  uint32_t pct = (used_kb * 100) / mem.total_ram_kb;

  if (pct >= 98) {
    // Auto-kill heaviest non-critical process
    system_kill_heaviest_non_critical();
    kitt_notify("Low memory: an app was closed automatically.");
  } else if (pct >= 95) {
    // Urgent dialog
    kitt_popup("Memory Critical",
               "Memory is almost full.\nClose an app to continue.", "OK");
  } else if (pct >= 90) {
    // Warning overlay via explorer.paw
    kitt_notify("Memory warning: consider closing an app.");
  }
}
```

---

## Stage 5: bridge.paw Init

```cpp
extern "C" int app_create(lv_obj_t* parent) {

  // 1. Load keymap from path declared in device.json
  device_config_t* cfg = kitt_get_device_config();
  if (!keymap_load(cfg->keymap_path)) {
    Serial.println("[BRIDGE] Keymap load failed — using raw GPIO fallback");
    keymap_set_passthrough(true);
  }

  // 2. Register raw key event reader from KITT
  kitt_set_reserved_combo_callback(bridge_reserved_combo_cb);

  // 3. Start key mapping loop (lv_timer, every 10ms)
  lv_timer_create(key_mapping_tick, 10, NULL);

  // 4. Register radio handoff broker with system.paw
  system_set_handoff_broker(bridge_handoff_request, bridge_handoff_reclaim);

  return 0;
}

static void key_mapping_tick(lv_timer_t* t) {
  kitt_raw_key_event_t raw;
  while (kitt_get_raw_key_event(&raw)) {
    // Reserved combo check — always intercepted
    if (keymap_is_reserved_combo(&raw)) {
      bridge_reserved_combo_cb();
      continue;
    }
    // Map raw GPIO → generic keycode
    kitt_generic_key_t mapped = keymap_translate(&raw);
    // Inject into KITT for LVGL / firmware delivery
    kitt_inject_key(mapped, raw.pressed);
  }
}

static void bridge_reserved_combo_cb(void) {
  // Force-kill whatever firmware is running
  if (system_firmware_running()) {
    char fw_path[128];
    system_get_firmware_path(fw_path, sizeof(fw_path));
    kitt_process_kill(fw_path);
    system_reclaim_radios();
    kitt_notify("Firmware force-killed. Radios reclaimed.");
  }
}
```

---

## Stage 6: explorer.paw Init

```cpp
extern "C" int app_create(lv_obj_t* parent) {

  // 1. Register KITT callbacks
  kitt_set_tray_update_cb(explorer_update_tray);
  kitt_set_popup_cb(explorer_show_popup);
  kitt_set_notify_cb(explorer_show_toast);
  kitt_set_crash_report_cb(explorer_show_crash_report);
  kitt_set_memory_warning_cb(explorer_memory_warning);

  // 2. Build taskbar (persistent — never destroyed while explorer is alive)
  explorer_build_taskbar(parent);

  // 3. Build desktop / app area
  explorer_build_desktop(parent);

  // 4. Query KITT for app + firmware lists
  explorer_list_refresh();

  // 5. Signal system.paw that UI is ready
  system_ready();

  return 0;
}
```

---

## Flasher Mode Boot Path

Triggered when KITT reads a non-zero boot flag in NVS at Step 8. Takes over completely — never returns to normal boot.

```
Normal boot → Step 8: boot flag SET
      │
      ▼
flasher_run()
      │
      ├─ Init display (text mode only)
      ├─ Init keypad (raw GPIO, no LVGL)
      ├─ Print "PURR OS Flasher" on display
      │
      ├─ Check /update/ for staged image:
      │     update_firmware.purr   ← preferred
      │     update_firmware.bin    ← also accepted
      │
      ├─ Image found:
      │     Print "Writing image..."
      │     Call ESP OTA write API
      │     Success → clear boot flag → delete staged file → reboot
      │     Fail    → print "ERR: Flash failed" → clear flag → reboot
      │
      └─ Image NOT found:
            Print "ERR: No image in /update/"
            Print "Drop .purr in /update/ via MTP"
            Wait 10s for user key press OR timeout
            Clear boot flag → reboot into normal OS
```

---

## Emergency Recovery Boot Path

Triggered by:
- Hardware key combo held at power-on (before KITT loads)
- watchdog.purr cannot restore KITT from backup
- KITT calls `watchdog_trigger_emergency()` on unrecoverable error

```
Emergency triggered
      │
      ▼
emergency.purr
      │
      ├─ Mount filesystem (direct, no KITT)
      ├─ Init display (minimal, text mode only)
      ├─ Init keypad (raw GPIO)
      │
      ├─ Print:
      │     "PURR OS Emergency Recovery"
      │     "Connect USB for MTP access"
      │     "Drop new OS image in /update/"
      │
      ├─ Enable MTP USB mode
      │     User connects PC
      │     User drops new .purr / .bin into /update/
      │
      ├─ Detect file in /update/:
      │     Prompt: "Flash now? [SEL=Yes] [BACK=Cancel]"
      │     Yes → flash via OTA API → reboot
      │     Cancel → reboot without flashing
      │
      └─ No user action after 5 minutes → reboot
```

This path has zero dependency on KITT, system.paw, or LVGL. It runs from a standalone binary in `/boot/emergency.purr` with its own minimal display + USB stack.

---

## NVS Keys — Full Reference

| Key | Type | Written by | Read by | Purpose |
|---|---|---|---|---|
| `boot_flag` | u8 | ota.paw, system.paw | KITT (Step 8) | Triggers flasher mode when non-zero |
| `kitt_hb` | u32 | KITT (every 500ms) | watchdog | KITT alive heartbeat timestamp |
| `sys_hb` | u32 | system.paw (every 500ms) | watchdog | system.paw alive heartbeat |
| `kitt_ready` | u8 | KITT (Step 18) | watchdog, system.paw | Signals KITT init complete |
| `wdt_boot_time` | u32 | watchdog | watchdog | Boot timestamp for confirmed-good timer |
| `wifi_ssid` | str | controlpanel.paw | KITT (Step 9) | Last connected WiFi SSID |
| `wifi_pass` | str | controlpanel.paw | KITT (Step 9) | Last connected WiFi password |
| `lora_freq` | u32 | controlpanel.paw | KITT (Step 11) | Last LoRa frequency |
| `lora_power` | u8 | controlpanel.paw | KITT (Step 11) | Last LoRa TX power |
| `cpu_freq` | u16 | controlpanel.paw | KITT (Step 14) | Last CPU frequency setting |
| `verbose_boot` | u8 | device.json | watchdog, KITT | Verbose logging toggle |
| `otadata` | blob | ESP OTA API | ESP-IDF bootloader | OTA partition selector |

---

## Boot Timing Estimates

| Stage | Heltec V3 | CattoPad |
|---|---|---|
| ESP-IDF bootloader | ~200ms | ~200ms |
| watchdog.purr init | ~100ms | ~100ms |
| KITT display init | ~300ms | ~500ms (ILI9488 slower than SSD1306) |
| KITT module loading | ~400ms | ~600ms |
| friends + apps scan | ~300ms | ~500ms |
| system.paw + bridge.paw | ~200ms | ~300ms |
| explorer.paw + UI ready | ~500ms | ~800ms |
| **Total (cold, no WiFi reconnect)** | **~2.0s** | **~3.0s** |
| WiFi reconnect (if saved SSID) | +1–3s | +1–3s |
| **Total (cold, with WiFi)** | **~3–5s** | **~4–6s** |

WiFi reconnect is non-blocking — explorer.paw becomes interactive before WiFi is confirmed. The tray icon updates to show connected state when KITT's async connect completes.

---

## Restart Scenarios

| Scenario | Who detects | Who restarts | What restarts |
|---|---|---|---|
| KITT heartbeat lost (3s) | watchdog | watchdog | KITT only |
| system.paw heartbeat lost (3s) | watchdog | watchdog via KITT signal | system.paw only |
| explorer.paw crash | system.paw | system.paw | explorer.paw only (with crash report) |
| OTA update triggered | ota.paw → system.paw | system.paw sets boot flag | Full system reboot |
| Reserved combo | bridge.paw | bridge.paw | Firmware kill only, OS stays up |
| KITT unrecoverable | watchdog | watchdog | emergency.purr |
| Manual reboot | user via controlpanel | system.paw | Full system reboot |

