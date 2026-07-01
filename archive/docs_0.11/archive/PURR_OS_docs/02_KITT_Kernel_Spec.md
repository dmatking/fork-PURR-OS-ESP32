# KITT Kernel — Specification
**v0.3.0**

---

## Overview

KITT (Kernel Interface Translation Toolkit) is the C++ hardware abstraction layer for PURR OS. It is the only layer that directly touches hardware — display, GPIO, radios, NVS, timers. Everything above it (system task, MiniWin shells, MicroPython apps) calls KITT APIs. KITT is built with ESP-IDF 5.3.5 + arduino-esp32 3.1.x.

---

## File Structure

```
CoreOS/system/kernel/
├── main.cpp                        — Arduino entry point (setup + no loop — FreeRTOS)
├── kitt.h                          — Full public API
├── kitt.cpp                        — Implementation
├── device_config.h/.cpp            — Parses device.json into device_config_t
├── idf_compat.c                    — IDF 5.x compat shims
├── devices/                        — Per-target JSON hardware profiles
│   ├── cyd.json
│   ├── heltec.json
│   └── tdeck.json
└── modules/
    ├── display_ili9341.h/.cpp      — ILI9341 320×240 driver (CYD)
    ├── display_ssd1306.h/.cpp      — SSD1306 128×64 OLED driver (Heltec)
    ├── touch_cst816s.h/.cpp        — CST816S capacitive touch (CYD)
    ├── wifi_manager.h/.cpp         — WiFi: scan, connect, NVS creds
    ├── bt_manager.h/.cpp           — Bluetooth BLE + Classic
    ├── lora_manager.h/.cpp         — LoRa radio (swappable backend)
    ├── power_manager.h/.cpp        — Battery ADC, CPU freq scaling
    ├── partition_manager.h/.cpp    — OTA slot management (CYD)
    ├── purr_bootloader.h/.cpp      — Factory image slot-scanner UI (cyd_boot)
    ├── blackberry_ui.h/.cpp        — BB-style shell (MiniWin)
    ├── explorer.h/.cpp             — Windows CE/PDA shell (MiniWin)
    ├── mtp_manager.h/.cpp          — MTP USB file transfer
    └── flasher.h/.cpp              — OTA partition flasher
```

---

## main.cpp — Entry Point

```cpp
#include <Arduino.h>
#include "kitt.h"
#include "device_config.h"

KITT kitt;

void setup() {
    Serial.begin(115200);
    kitt.init();
    // system_start() is called inside kitt.init() as a FreeRTOS task
}

void loop() {
    // Empty — FreeRTOS tasks drive everything
    vTaskDelay(portMAX_DELAY);
}
```

---

## kitt.h — Full Public API

### Lifecycle

```cpp
class KITT {
public:
    // Initialise all hardware, spawn system task.
    // Reads device.json from SPIFFS, loads modules, writes KITT_READY to NVS.
    void init();

    // Main tick — called by system task on a 10ms FreeRTOS tick.
    // Handles: input polling, heartbeat NVS write, radio status refresh.
    void update();

    // Graceful shutdown — quiesce radios, flush NVS, safe display state.
    void shutdown();

    bool is_ready();         // All critical modules initialised
    bool is_verbose();       // verbose_boot: true in device.json
    const char* device_name(); // Codename from device.json
    const char* os_name();     // "PURR OS" (LoRa present) or "PUR OS" (no radio)
```

### Display API

```cpp
    uint16_t display_width();    // Pixels — physical display width
    uint16_t display_height();

    // Row-based text output — available at all times, bypasses MiniWin.
    // Used for boot splash, crash reports, emergency output.
    void text_print(uint8_t row, const char* text);
    void text_clear();
    void text_set_colors(uint16_t fg_rgb565, uint16_t bg_rgb565);

    // Boot splash
    void show_boot_splash();

    // Serial + display logging (verbose mode)
    void log(const char* tag, const char* message);
    void log_errorf(uint8_t code, const char* fmt, ...);
```

### Input API

```cpp
    // Generic keycodes — output of the bridge keymap layer
    typedef enum {
        KEY_NONE = 0,
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
        KEY_SELECT, KEY_BACK, KEY_MENU,
        KEY_POWER,
        KEY_SOFT1, KEY_SOFT2,
    } generic_key_t;

    // Read raw GPIO key event (bridge reads this each tick)
    typedef struct {
        uint8_t  gpio_pin;
        bool     pressed;
        uint32_t timestamp_ms;
    } raw_key_event_t;
    bool get_raw_key_event(raw_key_event_t* out); // false if no event

    // Inject mapped generic key (bridge calls this after keymap lookup)
    void inject_key(generic_key_t key, bool pressed);

    // Read injected generic key (C++ shells call this — no LVGL needed)
    bool get_key_event(generic_key_t* out, bool* pressed);

    // Touch (CYD — CST816S)
    typedef struct {
        bool    pressed;
        uint8_t gesture;   // 0=none, 1=swipe up, 2=dn, 3=left, 4=right, 5=tap
        int16_t x, y;      // landscape coords 0..319, 0..239
    } touch_event_t;
    bool get_touch_event(touch_event_t* out); // false if no touch or not loaded
```

### WiFi API

```cpp
    void wifi_enable();
    void wifi_disable();
    bool wifi_enabled();

    // Non-blocking async scan
    void wifi_scan_start();
    bool wifi_scan_done();
    int  wifi_scan_count();
    void wifi_scan_get_ssid(int index, char* buf, size_t len);
    int  wifi_scan_get_rssi(int index);     // dBm
    bool wifi_scan_get_secured(int index);

    // Connection (async — poll wifi_connected())
    void wifi_connect(const char* ssid, const char* password);
    void wifi_disconnect();
    bool wifi_connected();
    void wifi_get_connected_ssid(char* buf, size_t len);
    int  wifi_signal_strength();    // dBm

    void wifi_forget(const char* ssid);
    void wifi_yield();
    void wifi_reclaim();
    bool wifi_yielded();
```

### Bluetooth API

```cpp
    void bt_enable();
    void bt_disable();
    bool bt_enabled();

    int  bt_paired_count();
    void bt_get_paired_name(int index, char* buf, size_t len);
    void bt_get_paired_addr(int index, char* buf, size_t len);
    bool bt_device_connected(int index);

    void bt_start_discovery(uint32_t timeout_ms);
    void bt_stop_discovery();
    bool bt_discovery_active();
    int  bt_discovered_count();
    void bt_get_discovered_name(int index, char* buf, size_t len);
    void bt_get_discovered_addr(int index, char* buf, size_t len);

    void bt_pair(int discovered_index);
    void bt_unpair(int paired_index);

    void bt_yield();
    void bt_reclaim();
    bool bt_yielded();
```

### LoRa API
*(No-op when `PURR_ENABLE_LORA=0` or on CYD targets — CYD has no LoRa hardware)*

```cpp
    void lora_enable();
    void lora_disable();
    bool lora_enabled();

    void     lora_set_frequency(uint32_t freq_hz);   // e.g. 915000000
    void     lora_set_power(uint8_t dbm);
    void     lora_set_spreading_factor(uint8_t sf);  // 7–12
    void     lora_set_bandwidth(uint32_t bw_hz);     // 125000 / 250000 / 500000
    void     lora_set_coding_rate(uint8_t cr);       // 5–8
    void     lora_set_sync_word(uint8_t sw);         // 0x12 private, 0x34 Meshtastic

    uint32_t lora_get_frequency();
    uint8_t  lora_get_power();
    int      lora_get_rssi();    // Last packet RSSI dBm
    float    lora_get_snr();     // Last packet SNR dB

    bool   lora_send(const uint8_t* data, size_t len);
    bool   lora_busy();
    bool   lora_data_available();
    size_t lora_read(uint8_t* buf, size_t max_len);

    void lora_yield();
    void lora_reclaim();
    bool lora_yielded();
```

### Power API

```cpp
    int  battery_percent();      // 0–100
    int  battery_voltage_mv();   // e.g. 3800
    bool battery_charging();

    void cpu_set_freq_mhz(int mhz);  // 80, 160, 240
    int  cpu_get_freq_mhz();
```

### App & Firmware Management

```cpp
    typedef struct {
        char     name[64];
        char     path[128];
        char     version[16];
        bool     needs_wifi;
        bool     needs_bt;
        bool     needs_lora;
        uint32_t min_ram_kb;
    } app_entry_t;

    void apps_scan();
    void firmware_scan();
    int  app_list_count();
    int  firmware_list_count();
    void app_get_entry(int index, app_entry_t* out);

    bool     app_launch(const char* path);      // runs via mpython_exec_app()
    void     process_kill(const char* path);
    bool     process_running(const char* path);
    uint32_t process_ram_usage_kb(const char* path);
```

### Partition Manager API
*(CYD only — compiled when `PURR_HAS_PARTITION_MGR`)*

```cpp
    // Declared in partition_manager.h — called directly, not via KITT class

    int  pm_boot_slot();          // Returns active OTA slot (0/1/…) or -1 if factory
    bool pm_boot_to_factory();    // Set factory as boot target + esp_restart()

    pm_slot_info_t pm_slot_info(uint8_t slot);  // Size, address, valid flag, NVS name
    bool pm_launch(uint8_t slot);               // Set OTA boot target + restart
    bool pm_delete(uint8_t slot);               // Erase slot + reset otadata if needed
    bool pm_install(uint8_t slot, const char* src_path, const char* name,
                    pm_progress_cb_t cb);        // Stream .bin from SPIFFS → OTA
```

### Memory API

```cpp
    typedef struct {
        uint32_t total_ram_kb;
        uint32_t free_ram_kb;
        uint32_t psram_total_kb;
        uint32_t psram_free_kb;
        uint32_t largest_free_block_kb;
    } memory_stats_t;

    void memory_get_stats(memory_stats_t* out);
    int  free_ram_kb();    // shorthand
    int  uptime_ms();
```

### Callbacks

```cpp
    typedef struct {
        int  battery_percent;
        int  battery_voltage_mv;
        bool wifi_connected;
        char wifi_ssid[32];
        int  wifi_rssi;
        bool bt_enabled;
        bool lora_enabled;
        int  lora_rssi;
        char time_str[16];  // "HH:MM"
    } tray_state_t;

    void set_tray_update_cb(void (*cb)(const tray_state_t*));
    void set_popup_cb(void (*cb)(const char* title, const char* msg, const char* btn));
    void set_notify_cb(void (*cb)(const char* message));
    void set_crash_report_cb(void (*cb)(const char* app_name, const char* reason));
    void set_memory_warning_cb(void (*cb)(int percent_used));  // fires at 90/95/98%
};
```

---

## Boot Sequence — KITT::init()

```
1.  Serial.begin(115200)
2.  Mount SPIFFS
3.  Parse device.json via ArduinoJson → device_config_t
        on fail: emergency_text() + halt
4.  Init display (ILI9341 or SSD1306 based on TARGET_DEVICE)
        on fail: Serial only from here
5.  If verbose_boot: log to serial + display
    Else: show_boot_splash()
6.  Init wifi_manager (background connect to saved network)
7.  Init bt_manager (if PURR_HAS_BT)
        restore paired device list from NVS
8.  Init lora_manager (if PURR_HAS_LORA)
        on fail: log, continue without LoRa; os_name = "PUR OS"
9.  Init touch_cst816s (if PURR_HAS_TOUCH_CST816S)
10. Init power_manager
        read battery ADC (ADC1 only — ADC2 unusable when WiFi active)
        set CPU freq to device max
11. Init partition_manager (if PURR_HAS_PARTITION_MGR)
        scan OTA slots, build slot info table
12. apps_scan() → firmware_scan()
13. Write KITT_READY to NVS
14. Spawn system_task (FreeRTOS, core 1)
```

---

## Factory Image Startup — purr_bootloader

When `PURR_IS_BOOTLOADER_IMG` is defined (cyd_boot target), the system task calls `purr_bootloader_start()` instead of launching a UI shell.

```
purr_bootloader_start()
  → pm_boot_slot()           determine if a slot is currently active
  → scan ota_0, ota_1        esp_ota_get_partition_description() for each
  → render home screen:
      for each slot:
          valid image  → BOOT button + WIPE button + firmware version label
          empty slot   → INSTALL button
  → on BOOT tap    → confirm → pm_launch(slot) → esp_restart()
  → on WIPE tap    → confirm → pm_delete(slot) → refresh UI
  → on INSTALL tap → (future: OTA install from SPIFFS or network)
  → blue LED heartbeat (GPIO2) every 500ms
```

---

## Watchdog Heartbeat

KITT writes a timestamp to NVS key `kitt_hb` every 500ms inside `KITT::update()`. A watchdog task reads it every 1000ms; if unchanged for 3000ms it calls `esp_restart()`.

```cpp
void KITT::update() {
    uint32_t now = millis();
    if (now - last_hb >= 500) {
        nvs_set_u32(nvs_handle, "kitt_hb", now);
        nvs_commit(nvs_handle);
        last_hb = now;
    }
    poll_gpio_inputs();
    refresh_radio_status_staggered(now);
    if (now - last_battery_refresh >= 60000) {
        power_manager_refresh_battery();
        last_battery_refresh = now;
    }
}
```

---

## Error Code Reference

| Code | Constant | Meaning |
|------|----------|---------|
| `0x01` | `E_JSON_PARSE` | device.json missing or malformed |
| `0x02` | `E_DISPLAY_INIT` | Display driver failed to init |
| `0x03` | `E_LORA_INIT` | LoRa radio did not respond |
| `0x04` | `E_WIFI_INIT` | WiFi subsystem failed |
| `0x05` | `E_TOUCH_INIT` | CST816S I2C not found |
| `0x06` | `E_FRIENDS_SCAN` | /friends/ filesystem error |
| `0x07` | `E_MINIWIN_INIT` | MiniWin HAL init failed |
| `0x08` | `E_SYSTEM_LAUNCH` | System task failed to spawn |
| `0x09` | `E_WDT_TIMEOUT` | Watchdog triggered restart |
| `0x0A` | `E_FLASH_FAIL` | OTA write failed |

Small display (OLED): `ERR:0x03`
Large display (ILI9341): `E_LORA_INIT_FAIL — Check SX1262 SPI pins`
