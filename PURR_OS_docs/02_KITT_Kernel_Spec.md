# KITT Kernel — Detailed Specification

## Overview

KITT (Kernel Interface Translation Toolkit) is the hardware abstraction layer for PURR OS. Written in C/C++, compiled via Arduino IDE. It is the only layer that directly touches hardware — display, GPIO, radios, timers, NVS. Everything above it (system.meow, bridge.meow, explorer.meow, user apps) calls KITT APIs. KITT never goes down with the UI — watchdog monitors it separately.

**Target size:** 400–600KB compiled (core ~100–150KB + conditionally loaded modules)

---

## File Structure

```
system/kernel.meow/
├── main.cpp                    # Arduino entry point — setup() + loop()
├── kitt.h                      # Public API header — all external callers use this
├── kitt.cpp                    # Core implementation
├── device_config.h             # Parsed device.json values as C structs
├── device.json                 # Hardware profile for this device
│
├── modules/
│   ├── display_ili9488.h/.cpp  # CattoPad display driver (TFT_eSPI + LVGL)
│   ├── display_ssd1306.h/.cpp  # Heltec V3 OLED driver
│   ├── touch_mxt336t.h/.cpp    # Atmel mXT336T capacitive touch driver
│   ├── wifi_manager.h/.cpp     # WiFi scan, connect, yield/reclaim
│   ├── bt_manager.h/.cpp       # Bluetooth pair, connect, yield/reclaim
│   ├── lora_manager.h/.cpp     # Heltec LoRa lib wrapper, yield/reclaim
│   ├── pi_manager.h/.cpp       # Pi power gate, handshake, display handoff
│   ├── mtp_manager.h/.cpp      # USB MTP mode for drag-and-drop file access
│   ├── flasher.h/.cpp          # Boot-flag flasher mode
│   └── power_manager.h/.cpp    # Battery ADC, CPU freq scaling, rail management
│
└── assets/
    ├── splash_cattopad.txt     # ASCII boot splash for 320x480
    └── splash_v3.txt           # ASCII boot splash for 128x64
```

---

## main.cpp — Arduino Entry Point

```cpp
#include <Arduino.h>
#include <lvgl.h>
#include "kitt.h"
#include "device_config.h"

KITT kitt;

void setup() {
  Serial.begin(115200);

  // Init KITT — reads device.json, loads modules, sets up hardware
  if (!kitt.init("/system/kernel.meow/device.json")) {
    // Critical failure — drop to emergency text mode
    Serial.println("[KITT] FATAL: init failed");
    kitt.emergency_text("KITT INIT FAIL", "See serial log", "Hold PWR to recover");
    while (true) delay(1000);
  }
}

void loop() {
  kitt.update();          // KITT main tick — input, radio status, heartbeat
  lv_timer_handler();     // LVGL render tick
  delay(5);               // ~200fps ceiling, LVGL handles frame pacing
}
```

---

## kitt.h — Full Public API

### Lifecycle

```cpp
class KITT {
public:
  // ─── Lifecycle ────────────────────────────────────────────────────────────

  // Read device.json, load modules, init hardware
  // Returns false if any critical module fails (display, core GPIO)
  bool init(const char* device_json_path);

  // Main update — call every loop iteration
  // Handles: input polling, heartbeat, radio status refresh, Pi state check
  void update();

  // Graceful shutdown — quiesce radios, flush NVS, safe display state
  void shutdown();

  // Emergency text output — bypasses LVGL, writes directly to display
  // Used when LVGL or display driver not yet initialised
  void emergency_text(const char* line1, const char* line2, const char* line3);

  // ─── Status ───────────────────────────────────────────────────────────────

  bool is_ready();            // All critical modules initialised
  bool is_in_flasher_mode();  // Boot flag was set — flasher took over
  bool is_verbose();          // verbose_boot: true in device.json
  const char* device_name();  // Returns device codename from device.json
```

### Display API

```cpp
  // ─── Display ──────────────────────────────────────────────────────────────

  uint16_t display_width();   // Pixels — from device.json display_res
  uint16_t display_height();

  // Text mode fallback — rendered directly when explorer.meow is absent or crashed
  // Rows are 0-indexed. Max chars per row depends on font size and display width.
  void text_print(uint8_t row, const char* text);
  void text_clear();
  void text_set_color(uint32_t fg_hex, uint32_t bg_hex);

  // Boot splash — reads ASCII art file from path in device.json
  void show_boot_splash();

  // Diagnostic logging (verbose mode only)
  // Logs to serial always; logs to display if display is large enough (>128px wide)
  void log(const char* tag, const char* message);
  void log_errorf(uint8_t code, const char* fmt, ...);

  // Pi display handoff (CattoPad only — no-op on other devices)
  void display_yield_to_pi();    // Release SPI to Pi
  void display_reclaim_from_pi(); // Take SPI back from Pi
  bool display_pi_owns();        // True if Pi currently holds SPI
```

### Input API

```cpp
  // ─── Input ────────────────────────────────────────────────────────────────

  // Raw hardware key event — read by bridge.meow for mapping
  typedef struct {
    uint8_t  gpio_pin;
    bool     pressed;       // true = press, false = release
    uint32_t timestamp_ms;
  } raw_key_event_t;

  // Generic keycodes — output of bridge.meow's mapping
  typedef enum {
    KEY_NONE = 0,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_SELECT, KEY_BACK, KEY_MENU,
    KEY_POWER,
    KEY_SOFT1, KEY_SOFT2,  // Device-specific soft keys
    KEY_RESERVED_COMBO     // Always captured by KITT — never forwarded to firmware
  } generic_key_t;

  // Poll latest raw key event (bridge.meow calls this each tick)
  // Returns false if no event pending
  bool get_raw_key_event(raw_key_event_t* out);

  // Inject generic key event (bridge.meow calls this after mapping)
  void inject_key(generic_key_t key, bool pressed);

  // Touch event (if touch module loaded)
  typedef struct {
    uint16_t x;
    uint16_t y;
    bool     pressed;
    uint8_t  contact_id;
  } touch_event_t;

  bool get_touch_event(touch_event_t* out);  // false if no touch or touch not loaded

  // Reserved combo callback — called when power+select is held
  // Fires even during /friends/ firmware exclusivity
  void set_reserved_combo_callback(void (*cb)(void));
```

### WiFi API

```cpp
  // ─── WiFi ─────────────────────────────────────────────────────────────────

  void wifi_enable();
  void wifi_disable();
  bool wifi_enabled();

  // Non-blocking async scan
  // Call wifi_scan_start(), poll wifi_scan_done(), then read results
  void wifi_scan_start();
  bool wifi_scan_done();
  int  wifi_scan_count();
  void wifi_scan_get_ssid(int index, char* buf, size_t len);
  int  wifi_scan_get_rssi(int index);    // dBm — typically -30 to -90
  bool wifi_scan_get_secured(int index); // true if password required

  // Connection — async, poll wifi_connected() after calling
  void wifi_connect(const char* ssid, const char* password);
  void wifi_disconnect();
  bool wifi_connected();
  void wifi_get_connected_ssid(char* buf, size_t len);
  int  wifi_signal_strength();           // dBm of current connection

  // Forget saved network
  void wifi_forget(const char* ssid);

  // Yield to /friends/ firmware
  // KITT stops using WiFi stack — firmware gets full ownership
  void wifi_yield();
  void wifi_reclaim();
  bool wifi_yielded();
```

### Bluetooth API

```cpp
  // ─── Bluetooth ────────────────────────────────────────────────────────────

  void bt_enable();
  void bt_disable();
  bool bt_enabled();

  // Paired device list (persisted to NVS)
  int  bt_paired_count();
  void bt_get_paired_name(int index, char* buf, size_t len);
  void bt_get_paired_addr(int index, char* buf, size_t len);  // MAC string
  bool bt_device_connected(int index);

  // Discovery — non-blocking, 30s window by default
  void bt_start_discovery(uint32_t timeout_ms);
  void bt_stop_discovery();
  bool bt_discovery_active();
  int  bt_discovered_count();
  void bt_get_discovered_name(int index, char* buf, size_t len);
  void bt_get_discovered_addr(int index, char* buf, size_t len);

  // Pair / unpair
  void bt_pair(int discovered_index);
  void bt_unpair(int paired_index);

  // Yield to /friends/ firmware
  void bt_yield();
  void bt_reclaim();
  bool bt_yielded();
```

### LoRa API

```cpp
  // ─── LoRa ─────────────────────────────────────────────────────────────────
  // Uses Heltec official Arduino LoRa library (SX1262)
  // No-op on devices without LoRa in device.json radios array

  void lora_enable();
  void lora_disable();
  bool lora_enabled();

  // Radio config
  void    lora_set_frequency(uint32_t freq_hz);   // e.g. 915000000 for US915
  void    lora_set_power(uint8_t dbm);             // 2–22 dBm (SX1262 max)
  void    lora_set_spreading_factor(uint8_t sf);   // 7–12
  void    lora_set_bandwidth(uint32_t bw_hz);      // 125000, 250000, 500000
  void    lora_set_coding_rate(uint8_t cr);        // 5–8 (denominator, 4/5 to 4/8)
  void    lora_set_sync_word(uint8_t sw);          // 0x12 private, 0x34 public (Meshtastic)

  uint32_t lora_get_frequency();
  uint8_t  lora_get_power();
  int      lora_get_rssi();  // Last packet RSSI dBm
  float    lora_get_snr();   // Last packet SNR dB

  // TX
  bool lora_send(const uint8_t* data, size_t len);
  bool lora_busy();  // True if TX in progress

  // RX
  bool   lora_data_available();
  size_t lora_read(uint8_t* buf, size_t max_len);

  // Emergency log TX (last-resort telemetry)
  // Reads /logs/boot.txt and transmits over LoRa in chunks
  void lora_transmit_log(const char* log_path);

  // Yield to /friends/ firmware
  void lora_yield();
  void lora_reclaim();
  bool lora_yielded();
```

### Battery & Power API

```cpp
  // ─── Power ────────────────────────────────────────────────────────────────
  // ADC1 only — ADC2 is unusable when WiFi is active on ESP32-S3

  int  battery_percent();       // 0–100
  int  battery_voltage_mv();    // Raw millivolts e.g. 3800
  int  battery_current_ma();    // 0 if not measurable
  bool battery_charging();      // True if charge pin detected high

  // CPU frequency scaling
  void cpu_set_freq_mhz(int mhz);   // Accepts 80, 160, 240 (or device max)
  int  cpu_get_freq_mhz();          // Current frequency

  // Rail management (CattoPad only — Pi power gate)
  void pi_rail_enable();
  void pi_rail_disable();
  bool pi_rail_enabled();
  bool pi_handshake_high();   // True if Pi has pulled handshake GPIO high
```

### App & Firmware API

```cpp
  // ─── App & Firmware Management ───────────────────────────────────────────

  // App entry — from /apps/ scan
  typedef struct {
    char name[64];
    char path[128];
    char version[16];
    bool is_lightweight;     // Can run as overlay alongside other apps
    bool needs_wifi;
    bool needs_bt;
    bool needs_lora;
    uint32_t min_ram_kb;
    char icon_path[128];
  } app_entry_t;

  // Firmware entry — from /friends/ scan
  typedef struct {
    char name[64];
    char path[128];
    char type[16];           // "firmware"
    char args[128];          // From friends.txt, e.g. "--headless"
    bool needs_wifi;
    bool needs_bt;
    bool needs_lora;
    bool in_manifest;        // True if listed in friends.txt
  } firmware_entry_t;

  // Discovery — call these once after boot or on hot-plug rescan
  void     apps_scan();                              // Re-scan /apps/
  void     firmware_scan();                          // Re-scan /friends/
  int      app_list_count();
  int      firmware_list_count();
  void     app_get_entry(int index, app_entry_t* out);
  void     firmware_get_entry(int index, firmware_entry_t* out);

  // Launch / kill
  bool     app_launch(const char* path);
  bool     firmware_launch(const char* path);
  void     process_kill(const char* path);
  bool     process_running(const char* path);
  uint32_t process_ram_usage_kb(const char* path);  // Heap used by process
```

### Memory API

```cpp
  // ─── Memory ───────────────────────────────────────────────────────────────

  typedef struct {
    uint32_t total_ram_kb;
    uint32_t free_ram_kb;
    uint32_t psram_total_kb;    // 0 if no PSRAM
    uint32_t psram_free_kb;
    uint32_t largest_free_block_kb;
  } memory_stats_t;

  void memory_get_stats(memory_stats_t* out);
```

### Callbacks (registered by explorer.meow at startup)

```cpp
  // ─── Callbacks ────────────────────────────────────────────────────────────

  // Tray update — KITT pushes radio/battery state every 15–60s
  typedef struct {
    int  battery_percent;
    int  battery_voltage_mv;
    bool wifi_connected;
    char wifi_ssid[32];
    int  wifi_rssi;
    bool bt_enabled;
    bool lora_enabled;
    int  lora_rssi;
    char time_str[16];         // "HH:MM"
  } tray_state_t;

  void set_tray_update_cb(void (*cb)(const tray_state_t* state));

  // Popup — KITT requests explorer.meow show a blocking dialog
  void set_popup_cb(void (*cb)(const char* title, const char* message, const char* btn_label));

  // Toast notification — non-blocking
  void set_notify_cb(void (*cb)(const char* message));

  // Crash report — system.meow passes crash data on app relaunch
  void set_crash_report_cb(void (*cb)(const char* app_name, const char* reason));

  // Memory warning — system.meow triggers these thresholds
  void set_memory_warning_cb(void (*cb)(int percent_used));  // 90, 95, 98
};
```

---

## Boot Sequence — Inside KITT::init()

```
1.  Open Serial at 115200 baud
2.  Mount SPIFFS / LittleFS filesystem
3.  Read and parse device.json (ArduinoJson)
        → On parse fail: error 0x01, drop to emergency_text()
4.  Set verbose flag from device.json
5.  Load display module (ili9488 or ssd1306)
        → On fail: error 0x02, Serial only from here
6.  Init LVGL + register display flush callback
        → On fail: error 0x07
7.  If verbose_boot = true:
        → Start logging to serial + display
    Else:
        → show_boot_splash() from splash file path
8.  Check NVS boot flag
        → If set: enter flasher mode (flasher.cpp takes over, never returns here)
9.  Load wifi_manager — connect to saved network if available
        → Non-blocking, connection attempted in background
10. Load bt_manager
        → Restore paired device list from NVS
11. Load lora_manager (if "lora" in radios)
        → On fail: log error 0x03, continue without LoRa
12. Load touch_mxt336t (if touch != "none")
        → On fail: log error 0x05, continue without touch
13. Load pi_manager (if pi_slot = true)
        → Read handshake GPIO
        → Build Pi state matrix (see Pi Power Management)
14. Load power_manager
        → Read battery ADC (ADC1 only)
        → Set CPU freq to device cpu_max_mhz
15. Scan /friends/ — call firmware_scan()
        → On fail: log error 0x06, continue (Start menu shows no firmware)
16. Scan /apps/ — call apps_scan()
17. Register LVGL input device (touch or keypad)
18. Write KITT_READY flag to NVS
19. Send first heartbeat to watchdog NVS key
20. Spawn system.meow
        → On fail: error 0x08, render text fallback + wait for watchdog restart
```

---

## Flasher Mode (flasher.cpp)

Triggered when KITT reads the boot flag in NVS at boot step 8. KITT never returns to normal boot — flasher takes over.

```cpp
void flasher_run(device_config_t* cfg) {
  // Minimal init — display + keypad only, no radios, no LVGL full stack
  display_init_text_mode(cfg);
  keypad_init_raw(cfg);

  display_text_print(0, "PURR OS Flasher");
  display_text_print(1, "Looking for image...");

  // Check /update/ for staged image
  if (!spiffs_file_exists("/update/update_firmware.bin") &&
      !spiffs_file_exists("/update/update_firmware.purr")) {
    display_text_print(2, "ERR: No image found");
    display_text_print(3, "Drop .bin/.purr in /update/");
    display_text_print(4, "Hold PWR to cancel");
    wait_for_key_or_timeout(10000);
    nvs_clear_boot_flag();
    esp_restart();
    return;
  }

  display_text_print(2, "Writing image...");

  // Flash the staged image via ESP OTA API
  bool ok = ota_write_image("/update/update_firmware.bin");

  if (ok) {
    display_text_print(3, "Done. Rebooting...");
    nvs_clear_boot_flag();
    spiffs_delete("/update/update_firmware.bin");
    delay(1500);
    esp_restart();
  } else {
    display_text_print(3, "ERR: Flash failed");
    display_text_print(4, "Image kept. Try again.");
    nvs_clear_boot_flag();
    delay(3000);
    esp_restart();
  }
}
```

---

## Radio Handoff State Machine

```
State: KITT_OWNS
│
├─ firmware requests WiFi/BT/LoRa via IPC
│       ↓
State: YIELDING
│  KITT quiesces its own stack use
│  Calls wifi_yield() / bt_yield() / lora_yield()
│       ↓
State: FIRMWARE_OWNS
│  Firmware has full radio exclusivity
│  KITT retains: display, touch, keypad, battery, reserved combo
│  Lightweight overlays: allowed if free RAM > friends_ram_threshold_kb
│       ↓
├─ firmware exits normally  OR  reserved combo triggered
│       ↓
State: RECLAIMING
│  KITT calls wifi_reclaim() / bt_reclaim() / lora_reclaim()
│  Restores radio init state from before yield
│       ↓
State: KITT_OWNS
```

KITT's reserved combo (power + select) fires a hard kill via `set_reserved_combo_callback`. The callback in system.meow calls `process_kill(firmware_path)`, then signals KITT to reclaim.

---

## Watchdog Heartbeat Protocol

KITT writes a timestamp to NVS every 500ms. watchdog.bin reads it every 1000ms. If unchanged for 3000ms, watchdog restarts KITT.

```cpp
// In KITT::update() — called every loop iteration
static uint32_t last_heartbeat_ms = 0;

void KITT::update() {
  uint32_t now = millis();

  // Heartbeat
  if (now - last_heartbeat_ms >= 500) {
    nvs_set_u32(nvs_handle, "kitt_hb", now);
    nvs_commit(nvs_handle);
    last_heartbeat_ms = now;
  }

  // Input polling
  poll_gpio_inputs();

  // Radio status refresh (staggered)
  refresh_radio_status_staggered(now);

  // Pi state check (CattoPad only)
  if (cfg.pi_slot) pi_manager_update();

  // Battery refresh every 60s
  if (now - last_battery_refresh >= 60000) {
    power_manager_refresh_battery();
    last_battery_refresh = now;
  }
}
```

---

## Display Module: display_ili9488.cpp

```cpp
#include <TFT_eSPI.h>
#include <lvgl.h>

// Pin config — set in TFT_eSPI User_Setup.h for CattoPad
// #define TFT_MOSI   11
// #define TFT_SCLK   12
// #define TFT_CS     10
// #define TFT_DC      9
// #define TFT_RST     8
// #define TFT_BL     46   // Backlight — PWM for brightness control
// #define TFT_WIDTH  320
// #define TFT_HEIGHT 480

static TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[320 * 10];  // 10-line buffer (~6KB)
static lv_color_t buf2[320 * 10];  // Double buffer for smoother rendering

void display_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

void display_ili9488_init() {
  tft.begin();
  tft.setRotation(0);   // Portrait, USB at bottom for CattoPad

  // Backlight PWM
  ledcSetup(0, 5000, 8);       // Channel 0, 5kHz, 8-bit
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, 255);            // Full brightness

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 320 * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = 320;
  disp_drv.ver_res  = 480;
  disp_drv.flush_cb = display_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
}

void display_set_brightness(uint8_t level) {
  ledcWrite(0, level);  // 0–255
}
```

---

## Display Module: display_ssd1306.cpp

```cpp
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// Heltec V3 OLED — 128x64, I2C
// #define OLED_SDA 17
// #define OLED_SCL 18
// #define OLED_RST 21
// #define SCREEN_WIDTH  128
// #define SCREEN_HEIGHT  64

static Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

void display_ssd1306_init() {
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[KITT] SSD1306 init failed");
    return;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.display();
}

void display_ssd1306_text(uint8_t row, const char* text) {
  // Row height = 8px at text size 1, so max 8 rows on 64px display
  oled.fillRect(0, row * 8, SCREEN_WIDTH, 8, SSD1306_BLACK);
  oled.setCursor(0, row * 8);
  oled.print(text);
  oled.display();
}

void display_ssd1306_clear() {
  oled.clearDisplay();
  oled.display();
}
```

---

## LoRa Module: lora_manager.cpp

```cpp
#include "LoRa.h"  // Heltec official Arduino LoRa library

// Heltec V3 SX1262 pin config
// #define LORA_SCK   9
// #define LORA_MISO 11
// #define LORA_MOSI 10
// #define LORA_CS    8
// #define LORA_RST  12
// #define LORA_DIO1 14  // SX1262 uses DIO1 not DIO0
// #define LORA_BUSY 13

static bool lora_ready       = false;
static bool lora_yielded_flag = false;

void lora_manager_init(uint32_t freq_hz, uint8_t power_dbm) {
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO1);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  if (!LoRa.begin(freq_hz)) {
    Serial.println("[KITT] LoRa init failed");
    return;
  }

  LoRa.setTxPower(power_dbm);
  LoRa.setSpreadingFactor(7);         // Default SF7
  LoRa.setSignalBandwidth(125E3);     // 125kHz
  LoRa.setCodingRate4(5);             // 4/5
  LoRa.setSyncWord(0x12);             // Private network default
  LoRa.enableCrc();

  lora_ready = true;
  Serial.printf("[KITT] LoRa OK at %lu Hz, %d dBm\n", freq_hz, power_dbm);
}

bool lora_manager_send(const uint8_t* data, size_t len) {
  if (!lora_ready || lora_yielded_flag) return false;
  LoRa.beginPacket();
  LoRa.write(data, len);
  return LoRa.endPacket();  // blocking by default
}

bool lora_manager_data_available() {
  return (!lora_yielded_flag && LoRa.parsePacket() > 0);
}

size_t lora_manager_read(uint8_t* buf, size_t max_len) {
  size_t n = 0;
  while (LoRa.available() && n < max_len) {
    buf[n++] = LoRa.read();
  }
  return n;
}

int   lora_manager_rssi() { return LoRa.packetRssi(); }
float lora_manager_snr()  { return LoRa.packetSnr();  }

void lora_manager_yield()   { lora_yielded_flag = true;  LoRa.sleep(); }
void lora_manager_reclaim() { lora_yielded_flag = false; LoRa.idle();  }
bool lora_manager_yielded() { return lora_yielded_flag; }
```

---

## Pi Manager: pi_manager.cpp

```cpp
// CattoPad only — manages Pi CM4 power rail and display handoff

// #define PI_GATE_PIN      40   // HIGH = Pi rail enabled (P-MOSFET gate)
// #define PI_HANDSHAKE_PIN 41   // Pi pulls HIGH when active
// #define PI_UART_TX       43   // KITT → Pi signal (halt command)
// #define PI_UART_RX       44   // Pi → KITT (ACK)

typedef enum {
  PI_STATE_ABSENT,          // Handshake LOW, gate LOW
  PI_STATE_POWERING_UP,     // Handshake LOW, gate HIGH
  PI_STATE_ACTIVE,          // Handshake HIGH, gate HIGH
  PI_STATE_ANOMALY          // Handshake HIGH, gate LOW
} pi_state_t;

static pi_state_t pi_state = PI_STATE_ABSENT;

void pi_manager_init() {
  pinMode(PI_GATE_PIN, OUTPUT);
  pinMode(PI_HANDSHAKE_PIN, INPUT_PULLDOWN);
  digitalWrite(PI_GATE_PIN, LOW);

  // Read initial state
  pi_manager_update();
}

void pi_manager_update() {
  bool gate      = digitalRead(PI_GATE_PIN);
  bool handshake = digitalRead(PI_HANDSHAKE_PIN);

  if (!handshake && !gate)  pi_state = PI_STATE_ABSENT;
  if (!handshake &&  gate)  pi_state = PI_STATE_POWERING_UP;
  if ( handshake &&  gate)  pi_state = PI_STATE_ACTIVE;
  if ( handshake && !gate)  pi_state = PI_STATE_ANOMALY;

  // If Pi just became active: yield display
  static pi_state_t last_state = PI_STATE_ABSENT;
  if (pi_state == PI_STATE_ACTIVE && last_state != PI_STATE_ACTIVE) {
    kitt_display_yield_to_pi();
  }
  // If Pi just went absent: reclaim display
  if (pi_state == PI_STATE_ABSENT && last_state == PI_STATE_ACTIVE) {
    kitt_display_reclaim_from_pi();
  }
  last_state = pi_state;
}

void pi_power_on() {
  digitalWrite(PI_GATE_PIN, HIGH);
  // Poll handshake for up to 30s
  uint32_t start = millis();
  while (!digitalRead(PI_HANDSHAKE_PIN) && millis() - start < 30000) {
    delay(100);
  }
}

void pi_power_off() {
  // Signal Pi to halt via UART
  Serial1.begin(115200, SERIAL_8N1, PI_UART_RX, PI_UART_TX);
  Serial1.println("HALT");
  // Wait for handshake to go LOW (Pi confirms shutdown)
  uint32_t start = millis();
  while (digitalRead(PI_HANDSHAKE_PIN) && millis() - start < 10000) {
    delay(100);
  }
  digitalWrite(PI_GATE_PIN, LOW);
}
```

---

## Error Code Reference

| Code | Constant | Meaning |
|---|---|---|
| `0x01` | `E_JSON_PARSE` | device.json missing or malformed |
| `0x02` | `E_DISPLAY_INIT` | Display driver failed to init |
| `0x03` | `E_LORA_INIT` | LoRa SX1262 did not respond |
| `0x04` | `E_WIFI_INIT` | WiFi subsystem failed |
| `0x05` | `E_TOUCH_INIT` | mXT336T I2C not found |
| `0x06` | `E_FRIENDS_SCAN` | /friends/ filesystem error |
| `0x07` | `E_LVGL_INIT` | LVGL init failed |
| `0x08` | `E_SYSTEM_LAUNCH` | system.meow failed to launch |
| `0x09` | `E_WDT_TIMEOUT` | Watchdog triggered restart |
| `0x0A` | `E_FLASH_FAIL` | OTA write failed in flasher mode |

Small screen output: `ERR:0x03`
Large screen output: `E_LORA_INIT_FAIL — Check SX1262 SPI pins`

