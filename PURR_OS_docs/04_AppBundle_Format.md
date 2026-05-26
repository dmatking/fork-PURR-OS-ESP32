# PURR OS App Bundle Format — .paw Specification

## Overview

A `.paw` bundle is the standard app package format for PURR OS. It is a folder with a defined structure containing a compiled C binary, a manifest, and optional assets. KITT scans `/apps/` at boot and on hot-plug rescan, finds all valid `.paw` bundles, and registers them in the Start menu automatically. Drag a `.paw` folder into `/apps/` via MTP — it appears in the Start menu on next scan. No installer, no reboot required.

The `.purr` extension is for standalone native binaries (no bundle structure needed). The `.paw` extension is for full app bundles with manifest, assets, and metadata.

---

## Extension Summary

| Extension | Type | Structure | Where |
|---|---|---|---|
| `.paw` | App bundle | Folder with manifest + binary + assets | `/apps/` |
| `.purr` | Native binary | Single compiled executable | `/apps/purr/`, `/friends/` |
| `.bin` | Third-party firmware | Single compiled binary | `/friends/` |
| `.fw` | Alternate firmware | Single compiled binary | `/friends/` |

---

## .paw Bundle Structure

```
myapp.paw/
├── main.bin           # Compiled Arduino/C build output — the executable
├── manifest.json      # Required — app metadata and capability declarations
└── assets/            # Optional — icons, fonts, sounds, data files
    ├── icon_32.bmp    # 32x32 icon for Start menu (BMP, 16-bit color)
    ├── icon_16.bmp    # 16x16 icon for taskbar (optional)
    └── (any other static assets your app needs)
```

KITT identifies a valid `.paw` bundle by:
1. Folder name ends in `.paw`
2. `manifest.json` exists and is valid JSON
3. `main.bin` exists and is non-zero size

If any of these fail, the bundle is skipped and logged as a warning — it does not hard-fail the scan.

---

## manifest.json — Full Specification

```json
{
  "name":           "My App",
  "version":        "1.0.0",
  "author":         "Your Name",
  "description":    "Short description shown in Start menu tooltip",

  "is_lightweight": false,

  "needs_wifi":     false,
  "needs_bt":       false,
  "needs_lora":     false,

  "min_ram_kb":     64,
  "min_flash_kb":   128,

  "icon":           "assets/icon_32.bmp",
  "icon_small":     "assets/icon_16.bmp",

  "entry":          "main.bin",

  "args":           "",

  "autostart":      false,
  "autostart_delay_ms": 0,

  "target_devices": ["cattopad", "heltec_v3", "heltec_v4"],

  "kit_api_version": 1
}
```

### Field Reference

| Field | Type | Required | Description |
|---|---|---|---|
| `name` | string | Yes | Display name shown in Start menu and taskbar |
| `version` | string | Yes | Semver string e.g. `"1.2.0"` |
| `author` | string | No | Author name — shown in app info |
| `description` | string | No | Tooltip text in Start menu |
| `is_lightweight` | bool | Yes | `true` = overlay app, runs alongside others. `false` = fullscreen, exclusive |
| `needs_wifi` | bool | Yes | KITT pre-checks WiFi availability before launch |
| `needs_bt` | bool | Yes | KITT pre-checks BT availability before launch |
| `needs_lora` | bool | Yes | KITT pre-checks LoRa availability before launch |
| `min_ram_kb` | int | Yes | KITT refuses launch if free RAM is below this value |
| `min_flash_kb` | int | No | Minimum flash storage needed at runtime |
| `icon` | string | No | Path relative to bundle root for 32x32 Start menu icon |
| `icon_small` | string | No | Path relative to bundle root for 16x16 taskbar icon |
| `entry` | string | No | Binary filename — defaults to `main.bin` if omitted |
| `args` | string | No | Arguments passed to the binary at launch |
| `autostart` | bool | No | If `true`, KITT launches this app automatically after system.paw is ready |
| `autostart_delay_ms` | int | No | Milliseconds to wait before autostart |
| `target_devices` | array | No | List of device codenames this app supports. If omitted, runs on all devices |
| `kit_api_version` | int | Yes | KITT API version this app was built against — KITT refuses to run if incompatible |

---

## main.bin — The Executable

`main.bin` is the compiled output from your Arduino IDE build, renamed from the default `.bin` output. It is a standard ESP32-S3 executable binary. KITT loads and executes it via the ESP32 process API.

### Entry Point Convention

Every `.paw` app must export these three C functions. KITT calls them by name:

```cpp
// Called by KITT when the app is launched
// parent: the LVGL screen object to draw on
// Return 0 on success, non-zero on failure
int app_create(lv_obj_t* parent);

// Called by KITT every frame while the app is running
// Use this for polling, animation, state updates
// Return 0 to keep running, 1 to request graceful exit
int app_update(void);

// Called by KITT when the app is being closed (user or system request)
// Clean up all LVGL objects and free memory here
void app_destroy(void);
```

### Minimal App Template

```cpp
// myapp.paw/main.cpp
// Build this in Arduino IDE, copy output .bin → main.bin

#include <Arduino.h>
#include <lvgl.h>
#include "kitt_api.h"      // Copy from /system/kernel.paw/ — KITT public API
#include "system_api.h"    // Copy from /system/system.paw/ — system.meow public API

// ─── App state ────────────────────────────────────────────────────────────────

static lv_obj_t* my_screen = NULL;
static lv_obj_t* my_label  = NULL;
static lv_obj_t* my_btn    = NULL;

// ─── Event handlers ───────────────────────────────────────────────────────────

static void btn_click_cb(lv_event_t* e) {
  lv_label_set_text(my_label, "Button pressed!");
}

// ─── KITT lifecycle exports ───────────────────────────────────────────────────

extern "C" int app_create(lv_obj_t* parent) {
  my_screen = lv_obj_create(parent);
  lv_obj_set_size(my_screen, lv_obj_get_width(parent), lv_obj_get_height(parent));
  lv_obj_set_style_bg_color(my_screen, lv_color_hex(0xC0C0C0), 0);

  my_label = lv_label_create(my_screen);
  lv_label_set_text(my_label, "Hello from .paw!");
  lv_obj_align(my_label, LV_ALIGN_CENTER, 0, -20);

  my_btn = lv_btn_create(my_screen);
  lv_obj_set_size(my_btn, 120, 40);
  lv_obj_align(my_btn, LV_ALIGN_CENTER, 0, 20);
  lv_obj_t* lbl = lv_label_create(my_btn);
  lv_label_set_text(lbl, "Tap Me");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(my_btn, btn_click_cb, LV_EVENT_CLICKED, NULL);

  return 0;  // Success
}

extern "C" int app_update(void) {
  // Nothing to poll — LVGL handles events
  return 0;  // Keep running
}

extern "C" void app_destroy(void) {
  if (my_screen) {
    lv_obj_del(my_screen);
    my_screen = NULL;
    my_label  = NULL;
    my_btn    = NULL;
  }
}
```

---

## KITT API Available to .paw Apps

Apps include `kitt_api.h` from the KITT bundle and link against KITT at runtime. The full API is documented in `02_KITT_Kernel_Spec.md`. The subset most apps need:

```cpp
// ─── WiFi (only if needs_wifi: true in manifest) ──────────────────────────
bool    kitt_wifi_connected(void);
void    kitt_wifi_get_connected_ssid(char* buf, size_t len);
int     kitt_wifi_signal_strength(void);  // dBm

// ─── Bluetooth (only if needs_bt: true in manifest) ───────────────────────
bool    kitt_bt_enabled(void);
int     kitt_bt_paired_count(void);

// ─── LoRa (only if needs_lora: true in manifest) ──────────────────────────
bool    kitt_lora_enabled(void);
bool    kitt_lora_send(const uint8_t* data, size_t len);
bool    kitt_lora_data_available(void);
size_t  kitt_lora_read(uint8_t* buf, size_t max_len);
int     kitt_lora_get_rssi(void);

// ─── Battery & power ──────────────────────────────────────────────────────
int     kitt_battery_percent(void);
int     kitt_battery_voltage_mv(void);

// ─── Display ──────────────────────────────────────────────────────────────
uint16_t kitt_display_width(void);
uint16_t kitt_display_height(void);

// ─── Input ────────────────────────────────────────────────────────────────
// Apps receive input via LVGL event callbacks — no direct key polling needed.
// For raw key events (e.g. physical button on Heltec V3), register via LVGL indev.

// ─── App communication ────────────────────────────────────────────────────
// Request system keyboard overlay (lightweight, not a separate app launch):
void    system_request_keyboard(lv_obj_t* target_textarea);

// Request file picker overlay:
// Returns selected path via callback — pass NULL cb to ignore
void    system_request_file_picker(const char* start_path,
                                   void (*result_cb)(const char* path));

// Signal to system.meow that this app wants to close itself:
void    system_app_close(const char* app_name);
```

---

## .paw Manifest Parser — KITT Implementation

```cpp
// Called during apps_scan() — parses manifest.json for each .paw bundle

#include <ArduinoJson.h>

bool kitt_parse_paw_manifest(const char* bundle_path, app_entry_t* out) {
  char manifest_path[256];
  snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", bundle_path);

  File f = SPIFFS.open(manifest_path, "r");
  if (!f) {
    Serial.printf("[KITT] Missing manifest: %s\n", manifest_path);
    return false;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("[KITT] Bad manifest JSON: %s — %s\n", manifest_path, err.c_str());
    return false;
  }

  // Required fields — fail if missing
  if (!doc["name"] || !doc["version"] || doc["is_lightweight"].isNull()
      || doc["needs_wifi"].isNull() || doc["needs_bt"].isNull()
      || doc["needs_lora"].isNull() || doc["min_ram_kb"].isNull()
      || doc["kit_api_version"].isNull()) {
    Serial.printf("[KITT] Manifest missing required fields: %s\n", manifest_path);
    return false;
  }

  // Check API version compatibility
  int api_ver = doc["kit_api_version"];
  if (api_ver != KITT_API_VERSION) {
    Serial.printf("[KITT] API version mismatch: app=%d, KITT=%d — %s\n",
                  api_ver, KITT_API_VERSION, bundle_path);
    return false;
  }

  // Check device target list (optional field)
  if (doc.containsKey("target_devices")) {
    JsonArray targets = doc["target_devices"].as<JsonArray>();
    bool device_match = false;
    for (JsonVariant v : targets) {
      if (strcmp(v.as<const char*>(), KITT_DEVICE_NAME) == 0) {
        device_match = true;
        break;
      }
    }
    if (!device_match) {
      Serial.printf("[KITT] App not targeting this device — skipping: %s\n", bundle_path);
      return false;
    }
  }

  // Check main.bin exists
  char binary_path[256];
  const char* entry = doc["entry"] | "main.bin";
  snprintf(binary_path, sizeof(binary_path), "%s/%s", bundle_path, entry);
  if (!SPIFFS.exists(binary_path)) {
    Serial.printf("[KITT] Binary not found: %s\n", binary_path);
    return false;
  }

  // Populate app_entry_t
  strlcpy(out->name,     doc["name"]    | "Unknown",  sizeof(out->name));
  strlcpy(out->path,     bundle_path,                 sizeof(out->path));
  strlcpy(out->version,  doc["version"] | "0.0.0",    sizeof(out->version));
  out->is_lightweight = doc["is_lightweight"] | false;
  out->needs_wifi     = doc["needs_wifi"]     | false;
  out->needs_bt       = doc["needs_bt"]       | false;
  out->needs_lora     = doc["needs_lora"]     | false;
  out->min_ram_kb     = doc["min_ram_kb"]     | 64;

  const char* icon = doc["icon"] | "";
  if (strlen(icon) > 0) {
    snprintf(out->icon_path, sizeof(out->icon_path), "%s/%s", bundle_path, icon);
  } else {
    out->icon_path[0] = '\0';
  }

  return true;
}
```

---

## Pre-launch Checks — KITT Implementation

Before calling `app_create()`, KITT runs these checks. Any failure produces an explorer.paw dialog instead of launching:

```cpp
bool kitt_can_launch_app(const app_entry_t* app, char* reason_buf, size_t reason_len) {

  // 1. RAM check
  memory_stats_t mem;
  kitt_memory_get_stats(&mem);
  if (mem.free_ram_kb < app->min_ram_kb) {
    snprintf(reason_buf, reason_len,
             "Not enough RAM.\nNeed %u KB, have %u KB free.",
             app->min_ram_kb, mem.free_ram_kb);
    return false;
  }

  // 2. Radio checks
  if (app->needs_wifi && !kitt_wifi_enabled()) {
    snprintf(reason_buf, reason_len,
             "\"%s\" needs WiFi.\nEnable WiFi in Control Panel first.", app->name);
    return false;
  }
  if (app->needs_bt && !kitt_bt_enabled()) {
    snprintf(reason_buf, reason_len,
             "\"%s\" needs Bluetooth.\nEnable BT in Control Panel first.", app->name);
    return false;
  }
  if (app->needs_lora && !kitt_lora_enabled()) {
    snprintf(reason_buf, reason_len,
             "\"%s\" needs LoRa.\nEnable LoRa in Control Panel first.", app->name);
    return false;
  }

  // 3. Fullscreen exclusivity check
  if (!app->is_lightweight && system_any_fullscreen_running()) {
    char running_name[64];
    system_get_fullscreen_app_name(running_name, sizeof(running_name));
    snprintf(reason_buf, reason_len,
             "\"%s\" is already running.\nClose it before launching \"%s\"?",
             running_name, app->name);
    return false;
  }

  // 4. Firmware exclusivity check
  if (system_firmware_running()) {
    char fw_name[64];
    system_get_firmware_name(fw_name, sizeof(fw_name));

    memory_stats_t mem;
    kitt_memory_get_stats(&mem);
    device_config_t* cfg = kitt_get_device_config();

    if (!app->is_lightweight || mem.free_ram_kb < cfg->friends_ram_threshold_kb) {
      snprintf(reason_buf, reason_len,
               "\"%s\" is active.\nCannot launch apps during firmware exclusivity.", fw_name);
      return false;
    }
  }

  return true;  // All checks passed — safe to launch
}
```

---

## Hot-Plug Rescan

When a user drops a new `.paw` bundle via MTP while the OS is running, explorer.paw can request a rescan:

```cpp
// explorer.paw calls this when MTP reports a new file dropped in /apps/
void explorer_trigger_rescan(void) {
  kitt_apps_scan();     // Re-scan /apps/ — updates KITT's internal app list
  explorer_list_refresh();  // Re-query KITT and rebuild Start menu
}
```

KITT's `apps_scan()` is non-destructive — it merges new entries with existing ones and marks removed bundles as unavailable without crashing running apps.

---

## Building a .paw App — Step by Step

### 1. Set up Arduino IDE

- Install board: Heltec ESP32 Series Dev-boards (see Architecture doc for URL)
- Install libraries: lvgl, TFT_eSPI, ArduinoJson
- Board target: `Heltec WiFi LoRa 32(V3)` or `ESP32S3 Dev Module` for CattoPad

### 2. Copy the API headers

Copy these two files from the PURR OS system bundle into your sketch folder:
```
kitt_api.h      ← from /system/kernel.paw/
system_api.h    ← from /system/system.paw/
```

### 3. Write your app

Use the minimal template above as a starting point. Implement:
- `app_create(lv_obj_t* parent)` — build your UI here
- `app_update(void)` — called every frame, return 0 to keep running
- `app_destroy(void)` — clean up all LVGL objects and memory

### 4. Build in Arduino IDE

`Sketch → Export Compiled Binary`

This produces a `.bin` file in your sketch folder.

### 5. Assemble the bundle

```
mkdir myapp.paw
cp sketch.ino.bin myapp.paw/main.bin
cp manifest.json  myapp.paw/
mkdir myapp.paw/assets
cp icon_32.bmp    myapp.paw/assets/
```

### 6. Deploy

Connect device via USB → MTP mode → drag `myapp.paw/` into `/apps/` on the device. Explorer rescans automatically and adds it to the Start menu.

---

## Icon Format

| Property | Requirement |
|---|---|
| Format | BMP, 16-bit color (RGB565) |
| Start menu icon | 32×32 pixels |
| Taskbar icon | 16×16 pixels (optional) |
| Background | Transparent not supported — use `0xC0C0C0` (OS grey) as bg |

Convert PNG to BMP RGB565 using LVGL's online image converter:
`https://lvgl.io/tools/imageconverter`

---

## Error Handling in Apps

Apps should never crash KITT. Follow these rules:

```cpp
// Always null-check LVGL objects before use
if (my_label) lv_label_set_text(my_label, "text");

// Always free LVGL objects in app_destroy()
// lv_obj_del() recursively deletes children — delete root object only
if (my_screen) {
  lv_obj_del(my_screen);
  my_screen = NULL;
}

// Never call delay() in app_update() — it blocks KITT's update loop
// Use lv_timer_create() for periodic tasks instead

// Never directly access hardware — always go through kitt_api.h
// Bad:  WiFi.begin("ssid", "pass");
// Good: kitt_wifi_connect("ssid", "pass");

// If app_create() fails, return non-zero — KITT will abort launch cleanly
extern "C" int app_create(lv_obj_t* parent) {
  if (!some_required_resource) {
    return -1;  // KITT shows error dialog, does not call app_update or app_destroy
  }
  // ...
  return 0;
}
```

---

## .paw vs .purr — When to Use Which

| Use `.paw` when... | Use `.purr` when... |
|---|---|
| Your app has a UI | Your binary is standalone with no LVGL UI |
| You need assets (icons, data files) | You just want to run a compiled binary |
| You want Start menu integration with icon | You want to drop it in `/friends/` or `/apps/purr/` for KITT to run as firmware |
| You want autostart, device targeting, API version checks | You don't need a manifest |
| Building for end users | Building a dev tool or test binary |

