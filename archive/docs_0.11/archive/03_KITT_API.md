# KITT API Reference

KITT is the kernel singleton. Access it via `extern KITT kitt` (include `kitt.h`).

All methods are thread-safe unless noted. Hardware subsystems are only available if their corresponding `PURR_HAS_*` define is set.

---

## Lifecycle

```cpp
kitt.init();          // Initialize all enabled subsystems — called once from main
kitt.update();        // Periodic tick — call from main task loop
kitt.shutdown();      // Graceful shutdown (rarely needed; use esp_restart() instead)
kitt.emergency_text(const char *msg);  // Write text to display without WM (panic use)
```

---

## Display / logging

```cpp
kitt.text_print(const char *text);
kitt.text_clear();
kitt.text_set_color(uint16_t fg, uint16_t bg);
kitt.show_boot_splash();
kitt.log(const char *fmt, ...);   // printf-style; shows on display + serial
```

---

## Device info

```cpp
const char *kitt.device_name();       // e.g. "CYD-S024C"
const char *kitt.os_name();           // "PURR OS"
```

---

## WiFi  (`PURR_HAS_WIFI` — always compiled for CYD)

```cpp
bool  kitt.wifi_connected();
void  kitt.wifi_enable();
void  kitt.wifi_disable();
int   kitt.wifi_scan(wifi_ap_record_t *out, int max);   // returns count
bool  kitt.wifi_connect(const char *ssid, const char *password);
void  kitt.wifi_disconnect();
int   kitt.wifi_rssi();       // dBm
void  kitt.wifi_yield();      // pause WiFi stack (e.g. before heavy SPI)
void  kitt.wifi_reclaim();    // resume
```

---

## Bluetooth  (`PURR_HAS_BT`)

```cpp
bool  kitt.bt_enabled();
void  kitt.bt_enable();
void  kitt.bt_disable();
int   kitt.bt_scan(bt_device_t *out, int max);
bool  kitt.bt_pair(const char *address);
bool  kitt.bt_unpair(const char *address);
int   kitt.bt_paired_count();
const bt_device_t *kitt.bt_paired_device(int idx);
```

---

## LoRa  (`PURR_HAS_LORA`)

```cpp
bool  kitt.lora_ready();
bool  kitt.lora_send(const uint8_t *data, size_t len);
int   kitt.lora_receive(uint8_t *buf, size_t max_len);  // -1 = nothing waiting
int   kitt.lora_rssi();
float kitt.lora_snr();

// Config (call before sending)
void  kitt.lora_set_frequency(float mhz);
void  kitt.lora_set_power(int8_t dbm);
void  kitt.lora_set_spreading_factor(uint8_t sf);  // 7–12
void  kitt.lora_set_bandwidth(float khz);          // 125, 250, 500
void  kitt.lora_set_coding_rate(uint8_t cr);       // 5–8
void  kitt.lora_set_sync_word(uint16_t word);
```

---

## SD card

```cpp
bool  kitt.sd_available();   // true if SD mounted and ready
```

Files are accessible via standard POSIX calls on `/sdcard/`:
```cpp
FILE *f = fopen("/sdcard/data.txt", "r");
```

---

## Power

```cpp
int   kitt.battery_percent();    // 0–100, -1 if no battery sense
float kitt.battery_voltage();    // volts
float kitt.battery_current();    // mA (negative = discharging)
bool  kitt.battery_charging();
void  kitt.cpu_set_freq(int mhz);   // 80, 160, 240
```

---

## Memory

```cpp
memory_stats_t stats = kitt.memory_stats();
// stats.total_ram_kb, stats.free_ram_kb
// stats.total_psram_kb, stats.free_psram_kb
```

---

## App / process management

```cpp
// app_entry_t: name[32], path[256], is_admin
int   kitt.app_scan(app_entry_t *out, int max);   // scans /sdcard/apps
bool  kitt.app_launch(const char *path);
int   kitt.process_count();
```

---

## Firmware (OTA)

```cpp
// firmware_entry_t: slot(int), label[16], valid(bool), version[32]
int   kitt.firmware_scan(firmware_entry_t *out, int max);
bool  kitt.firmware_set_boot(int slot);
void  kitt.firmware_reboot();
```

---

## Input (generic key events)

```cpp
typedef enum {
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_SELECT, KEY_BACK, KEY_MENU, KEY_POWER,
    KEY_SOFT1, KEY_SOFT2, KEY_RESERVED_COMBO
} generic_key_t;

bool  kitt.key_available();
generic_key_t kitt.key_read();
void  kitt.key_inject(generic_key_t key);  // software-inject a key event
```

---

## Touch events (raw)

```cpp
typedef struct {
    int16_t x, y;
    bool pressed;
    uint8_t contact_id;
} kitt_touch_event_t;

bool  kitt.touch_available();
kitt_touch_event_t kitt.touch_read();
```

---

## Panic

```cpp
#include "purr_panic.h"

// Stop codes
PURR_STOP_DEADBEEF   // Memory corruption
PURR_STOP_APP_CRASH  // Application exception
PURR_STOP_CATFAIL    // Critical kernel failure
PURR_STOP_MEM_FULL   // Out of memory
PURR_STOP_WATCHDOG   // Hardware watchdog
PURR_STOP_HAL_FAIL   // HAL error

// Levels
PURR_PANIC_BLUE  // Recoverable — shows message, waits for reboot tap
PURR_PANIC_RED   // Critical — halts

purr_panic(PURR_STOP_APP_CRASH, PURR_PANIC_RED, "heap overflow in render");
```
