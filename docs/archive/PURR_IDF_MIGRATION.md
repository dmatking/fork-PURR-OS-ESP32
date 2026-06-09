# PURR OS — Arduino → Pure ESP-IDF Migration

**Started:** 2026-06-06  
**Target version:** v0.8.0

Goal: Remove all `espressif/arduino-esp32` dependency. Build against pure IDF 5.3.x only.  
Benefit: smaller binary, broader target support (S3, C3, WIP devices), no Arduino layer instability.

---

## Status

| # | Task | Status | Notes |
|---|------|--------|-------|
| 1 | Replace TFT_eSPI → esp_lcd ILI9341 | ✅ Done | display_ili9341.cpp rewritten, uses esp_lcd_new_panel_ili9341 |
| 2 | Replace Wire → IDF i2c_master | ✅ Done | touch_cst816s.cpp uses i2c_master_transmit_receive |
| 3 | Replace GPIO/pinMode/digitalWrite → IDF gpio | ✅ Done | purr_idf_compat.h wraps gpio_set_direction/level |
| 4 | Replace ledcAttach/ledcWrite → IDF LEDC | ✅ Done | display_ili9341.cpp uses ledc_*_config + purr_idf_compat.h shim |
| 5 | Replace delay()/millis() → vTaskDelay/esp_timer | ✅ Done | purr_idf_compat.h provides drop-in replacements |
| 6 | Replace Preferences → NVS API | ✅ Done | system/main.cpp uses nvs_open/get_u8/set_u8/commit |
| 7 | Replace SPIFFS → fopen on /spiffs VFS path | ✅ Done | system/main.cpp write_crash_log uses fopen |
| 8 | Replace String → std::string | ✅ Done | purr_idf_compat.h typedef String = std::string |
| 9 | Replace Serial → ESP_LOG via compat shim | ✅ Done | purr_idf_compat.h _PurrSerial wraps ESP_LOGI |
| 10 | Remove arduino-esp32 from idf_component.yml | ✅ Done | Commented out with migration note |
| 11 | Remove TFT_eSPI and arduino from all CMakeLists | ✅ Done | All targets updated |
| 12 | Full clean build — no Arduino dependency | 🔄 Pending | Run: rm -r build_cyd && idf.py build cyd |

---

## Replacement Reference

### 1. Display (TFT_eSPI → esp_lcd)

```cpp
// OLD
#include <TFT_eSPI.h>
static TFT_eSPI tft = TFT_eSPI();
tft.begin();
tft.setRotation(1);
tft.startWrite();
tft.setAddrWindow(x, y, w, h);
tft.pushBlock(color, count);
tft.endWrite();

// NEW
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
esp_lcd_panel_handle_t panel = NULL;
// init via esp_lcd_new_panel_ili9341()
esp_lcd_panel_draw_bitmap(panel, x, y, x+w, y+h, color_data);
```

### 2. I2C (Wire → IDF i2c_master)

```cpp
// OLD
#include <Wire.h>
Wire.begin(SDA, SCL);
Wire.beginTransmission(addr);
Wire.write(reg);
Wire.endTransmission();
Wire.requestFrom(addr, len);
Wire.read();

// NEW
#include "driver/i2c_master.h"
i2c_master_bus_handle_t bus;
i2c_master_dev_handle_t dev;
i2c_master_bus_config_t bus_cfg = { .i2c_port = I2C_NUM_0, .sda_io_num = SDA, .scl_io_num = SCL };
i2c_new_master_bus(&bus_cfg, &bus);
uint8_t buf[] = { reg, data };
i2c_master_transmit(dev, buf, sizeof(buf), 100);
```

### 3. GPIO

```cpp
// OLD
pinMode(pin, OUTPUT);
digitalWrite(pin, HIGH);
digitalRead(pin);

// NEW
#include "driver/gpio.h"
gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
gpio_set_level((gpio_num_t)pin, 1);
gpio_get_level((gpio_num_t)pin);
```

### 4. LEDC (backlight PWM)

```cpp
// OLD
ledcAttach(pin, freq, resolution);
ledcWrite(pin, duty);

// NEW
#include "driver/ledc.h"
ledc_timer_config_t timer = { .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_8_BIT, .timer_num = LEDC_TIMER_0,
    .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
ledc_timer_config(&timer);
ledc_channel_config_t ch = { .gpio_num = pin, .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 255 };
ledc_channel_config(&ch);
ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
```

### 5. Time

```cpp
// OLD
delay(100);
millis();

// NEW
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
vTaskDelay(pdMS_TO_TICKS(100));
(uint32_t)(esp_timer_get_time() / 1000ULL);
```

### 6. NVS (Preferences)

```cpp
// OLD
Preferences prefs;
prefs.begin("purr", false);
prefs.getString("key", "default");
prefs.putString("key", value);

// NEW
#include "nvs_flash.h"
#include "nvs.h"
nvs_handle_t h;
nvs_open("purr", NVS_READWRITE, &h);
char buf[64]; size_t len = sizeof(buf);
nvs_get_str(h, "key", buf, &len);
nvs_set_str(h, "key", value);
nvs_commit(h);
nvs_close(h);
```

### 7. SPIFFS

```cpp
// OLD
#include <SPIFFS.h>
SPIFFS.begin(true);
File f = SPIFFS.open("/path", "r");

// NEW
#include "esp_spiffs.h"
#include <stdio.h>
esp_vfs_spiffs_conf_t cfg = { .base_path = "/spiffs",
    .partition_label = NULL, .max_files = 10, .format_if_mount_failed = true };
esp_vfs_spiffs_register(&cfg);
FILE* f = fopen("/spiffs/path", "r");
```

### 8. String → std::string / char[]

```cpp
// OLD
String s = "hello";
s += " world";
s.c_str();
s.length();

// NEW
#include <string>
std::string s = "hello";
s += " world";
s.c_str();
s.size();
// Or plain char[] for simple cases
```

### 9. Serial → ESP_LOG

```cpp
// OLD
Serial.begin(115200);
Serial.printf("[tag] msg %d\n", val);
Serial.println("[tag] msg");

// NEW
#include "esp_log.h"
static const char* TAG = "purr";
// Serial.begin() → removed (UART0 initialized by IDF)
ESP_LOGI(TAG, "msg %d", val);
ESP_LOGE(TAG, "error msg");
ESP_LOGD(TAG, "debug msg");
```

---

## Files to touch

| File | Arduino APIs used |
|------|------------------|
| `display_ili9341.cpp` | TFT_eSPI, ledcAttach, ledcWrite, Serial |
| `touch_cst816s.cpp` | Wire, pinMode, digitalWrite, delay, Serial |
| `kitt.cpp` | Serial, Preferences, SPIFFS, String, delay |
| `device_config.cpp` | Serial, String, SPIFFS |
| `blackberry_ui.cpp` | Serial, pinMode, digitalWrite, millis, delay |
| `explorer.cpp` | Serial |
| `classicmac.cpp` | Serial |
| `purr_bootloader.cpp` | Serial, Preferences, pinMode, digitalWrite, delay, millis |
| `partition_manager.cpp` | Serial |
| `lua_runtime.cpp` | Serial, delay, millis |
| `stub_managers.cpp` | Serial |
| `system/main.cpp` | Serial |
| `kernel/main.cpp` | Serial |

---

## idf_component.yml changes

```yaml
# REMOVE:
espressif/arduino-esp32: "~3.1.0"

# ADD (if not already present):
# All IDF built-in drivers are available without explicit listing
```

## CMakeLists.txt changes

Remove `espressif__arduino-esp32` from all `REQUIRES` lists.  
Add individual IDF driver components as needed:
- `driver` (GPIO, LEDC, SPI, I2C)
- `esp_timer`
- `nvs_flash`
- `spiffs`
- `esp_lcd`
- `esp_log`

---

## Notes

- The fork that ported PURR to WIP devices stripped Arduino first — their approach validates this is the right path
- esp_lcd ILI9341 driver uses the same `startWrite+setAddrWindow+pushColors` concept as LVGL's flush callback — no display pipeline change needed
- LVGL already runs natively on IDF — no changes needed there
- Lua component already compiles without Arduino — no changes needed
- ArduinoJson can be replaced with cJSON (IDF built-in) or kept (it's IDF-compatible without Arduino)

---

**Delete when done:**
```powershell
rm PURR_IDF_MIGRATION.md
```
