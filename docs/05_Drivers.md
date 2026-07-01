# PURR OS — Drivers

## What a Driver Is

In PURR OS, a driver is a `.purr` binary that:
1. Initializes hardware
2. Registers a catcall struct with the kernel so all other modules can use it
3. Handles all hardware-specific details internally

Drivers have no access to each other. They communicate only through the catcall registry. A display driver never calls a touch driver; it registers `catcall_display_t` and stops there.

Drivers can also be **baked into a specialized kernel** rather than compiled as `.purr` blobs. In that case the driver source lives under `source/kernel/kernel_<device>/` and calls `purr_kernel_register_*()` directly at boot. The catcall interface is identical — only the loading path differs. See [13_Kernels.md](13_Kernels.md) for when to use baked-in drivers vs. `.purr` blobs.

---

## driver.pcat Format

Every `.purr` driver has a manifest at `source/drivers/<type>/<name>/driver.pcat`:

```toml
name              = "ili9341"
version           = "0.1.0"
type              = "display"           # display / touch / input / radio / gps
catcall_version   = 1                   # matches CATCALL_DISPLAY_VERSION

idf_min           = "5.3.0"
chip              = ["esp32", "esp32s3"]

kernel_min        = "0.9.0"
kernel_max        = ""                  # empty = no ceiling

module_type       = "PURR_MOD_DRIVER"
provides          = ["catcall_display"]
required_catcalls = []
```

`modulestrap` reads this before compiling. `purrstrap` uses it to resolve which driver blobs to include in a device's SPIFFS image.

---

## Existing Drivers

### Display Drivers

#### ili9341

- **Type:** display / SPI
- **Devices:** cyd, cyd_s024c, cyd_s028r
- **Interface:** SPI (MOSI/CLK/CS/DC/RST/BL)
- **Resolution:** configurable — 240×320 or 320×240 via MADCTL
- **Notes:** `display_madctl = 0x40` required for cyd_s028r orientation. DMA mode used for `push_pixels`.

#### axs15231b

- **Type:** display + touch / QSPI
- **Devices:** jc3248w535
- **Interface:** QSPI 4-bit parallel (CS/PCLK/D0–D3/BL/RST)
- **Resolution:** 480×320
- **Notes:** Display and touch controller are the same IC. Display driver registers `catcall_display_t`; a companion driver registers `catcall_touch_t` via I2C on the same chip. Must call `drv_init()` from `purr_kernel_register_display` path — do not call hardware init from anywhere else.

#### st7789

- **Type:** display / SPI (hand-rolled, no IDF lcd panel abstraction)
- **Devices:** tdeck_plus, tdeck, waveshare169
- **Interface:** SPI (CS/DC/MOSI/SCLK/RST/BL)
- **Resolutions:** 320×240 (tdeck_plus/tdeck), 240×280 (waveshare169)
- **Notes:** Hardware init must be called from `drv_init()`. Inversion command (0x21) applied for T-Deck Plus. Backlight driven via GPIO (GPIO 42 on T-Deck Plus).

#### ssd1306

- **Type:** display / I2C
- **Devices:** heltec
- **Interface:** I2C (address 0x3C)
- **Resolution:** 128×64 monochrome
- **Notes:** 1-bit framebuffer, push_pixels converts RGB565 → threshold. Used with oled_ui module only.

---

### Touch Drivers

#### xpt2046

- **Type:** touch / SPI
- **Devices:** cyd, cyd_s028r
- **Interface:** SPI, shares bus with display (separate CS)
- **Notes:** Resistive touch. 10-sample median filter. 3-point calibration on first boot stored in NVS. Tap targets should be ≥24 px because resistive touch has ~2–3 px jitter.

#### cst816s

- **Type:** touch / I2C capacitive
- **Devices:** cyd_s024c, waveshare169
- **Interface:** I2C address 0x15
- **Notes:** No calibration needed. INT pin optional — driver polls if INT not wired.

#### gt911

- **Type:** touch / I2C capacitive
- **Devices:** tdeck_plus
- **Interface:** I2C address 0x5D (default) or 0x14
- **Notes:** Multi-touch (up to 5 points), only the first is used. Address is determined by the INT pin level during power-on — INT LOW → 0x5D, INT HIGH → 0x14.

  **⚠️ IDF 5.3 regression:** `i2c_master_probe()` and `i2c_master_transmit()` return `ESP_ERR_INVALID_STATE` instead of a proper NACK response on T-Deck Plus hardware. This breaks GT911 discovery completely when using the IDF i2c_master stack. The gt911 `.purr` driver is therefore non-functional on T-Deck Plus with IDF 5.3.

  **Workaround:** The `kernel_tdeck_plus_arduino` specialized kernel uses **Arduino Wire** for I2C, which bypasses the IDF i2c_master stack entirely. GT911 is found at 0x5D immediately and all touch features work correctly. This is the production path for T-Deck Plus.

  **Status register protocol:** After every read where the buffer-ready bit (0x80) is set in register 0x814E, always write `0x00` back to 0x814E. Failure to clear it causes the GT911 to stop reporting further touches — the chip thinks the host is still processing the previous event.

  **Power-save keepalive:** The GT911 enters a low-power sleep mode after ~2 seconds of inactivity. Write `0x00` to command register 0x8040 at least every 2 seconds to keep it awake. If it enters sleep, it will not respond to reads until the keepalive is sent.

---

### Input Drivers

#### trackball

- **Type:** input / GPIO (interrupt-driven)
- **Devices:** tdeck_plus, tdeck
- **Interface:** 4 direction GPIOs + 1 click GPIO, active HIGH via ISR
- **T-Deck Plus GPIOs:** UP=3, DN=15, LT=1, RT=2, CLK=0
- **Notes:** Generates `INPUT_EVENT_POINTER` events with `delta_x`/`delta_y` = ±1. Cursor acceleration is applied at the UI layer. Must not share GPIOs with JTAG or strapping pins.

#### bbq20

- **Type:** input / I2C
- **Devices:** tdeck_plus
- **Interface:** I2C address 0x55, shared bus with GT911 (SDA=18, SCL=8)
- **Notes:** RP2040-based keyboard bridge. Read protocol is a plain 1-byte `requestFrom` — returns the ASCII code of the pressed key, or `0x00` if no key is pressed. No write preamble or register address needed before reading.

  **⚠️ Bus interaction with GT911:** Both BBQ20 and GT911 share the I2C bus. If `Wire.requestFrom` blocks waiting for BBQ20 (which sends a NACK when idle), it can corrupt the GT911 read timing. Fix: set `Wire.setTimeOut(50)` and check for NACK before reading keyboard (or simply use `requestFrom` with the timeout in place — it returns 0 bytes on NACK rather than hanging).

---

### Radio Drivers

#### sx1276

- **Type:** radio / SPI
- **Devices:** tdeck_plus
- **Notes:** LoRa radio. SPI bus shared with display (separate CS). Supports SF7–SF12, 125/250/500 kHz bandwidth, all standard coding rates. `catcall_radio_t` exposes `send`, `receive`, `rssi`, `snr`, `set_freq`, `set_power`.

#### sx1262

- **Type:** radio / SPI
- **Devices:** tdeck, heltec
- **Notes:** Same catcall interface as sx1276. SX1262 adds a BUSY pin that must be polled before SPI transactions. Driver handles this internally.

---

### GPS Drivers

#### generic_nmea

- **Type:** GPS / UART
- **Devices:** tdeck_plus
- **Notes:** Parses NMEA 0183 GGA and RMC sentences from any UART-connected GPS module. Runs a background FreeRTOS task. `catcall_gps_t.get_fix()` returns a `gps_fix_t` with lat, lon, speed, altitude, and fix quality. Cold fix time in open sky: 20–60 seconds. Driver probe timeout: 800 ms.

---

## Driver Compat Matrix

| Driver | ESP32 | ESP32-S3 | IDF path stable | Arduino Wire path |
|--------|-------|----------|-----------------|-------------------|
| ili9341 | ✓ | ✓ | ✓ | n/a |
| axs15231b | — | ✓ | ✓ | n/a |
| st7789 | — | ✓ | ✓ | n/a |
| ssd1306 | — | ✓ | ✓ | n/a |
| xpt2046 | ✓ | ✓ | ✓ | n/a |
| cst816s | ✓ | ✓ | ✓ | n/a |
| gt911 | — | ✓ | ✗ (IDF 5.3 regression) | ✓ (kernel_tdeck_plus_arduino) |
| trackball | — | ✓ | ✓ | ✓ |
| bbq20 | — | ✓ | n/a | ✓ (kernel_tdeck_plus_arduino) |
| sx1276 | — | ✓ | ✓ | n/a |
| sx1262 | ✓ | ✓ | ✓ | n/a |
| generic_nmea | ✓ | ✓ | ✓ | n/a |

---

## Writing a New Driver

### 1. Create the directory structure

```
source/drivers/<type>/<name>/
  driver.pcat     manifest
  <name>.h        public header (optional)
  <name>.c        implementation
  CMakeLists.txt  IDF component (SRCS + INCLUDE_DIRS + REQUIRES)
```

Or place in `user_drivers/<type>/<name>/` for community drivers — auto-scanned by purrstrap.

### 2. Write driver.pcat

```toml
name              = "my_display"
version           = "0.1.0"
type              = "display"
catcall_version   = 1
idf_min           = "5.3.0"
chip              = ["esp32", "esp32s3"]
kernel_min        = "0.9.0"
kernel_max        = ""
module_type       = "PURR_MOD_DRIVER"
provides          = ["catcall_display"]
required_catcalls = []
```

### 3. Implement the catcall

```c
#include "catcall_display.h"
#include "purr_kernel.h"
#include "purr_module.h"

static esp_err_t my_init(const display_config_t *cfg) { /* hw init */ return ESP_OK; }
static esp_err_t my_push_pixels(int x, int y, int w, int h, const uint16_t *px) { return ESP_OK; }
static esp_err_t my_fill_rect(int x, int y, int w, int h, uint16_t color) { return ESP_OK; }
static esp_err_t my_set_brightness(uint8_t lvl) { return ESP_OK; }
static void      my_get_info(display_info_t *out) {
    out->width = 320; out->height = 240; out->bits_per_pixel = 16;
    out->name = "my_display";
}
static esp_err_t my_deinit(void) { return ESP_OK; }

static const catcall_display_t s_catcall = {
    .name            = "my_display",
    .catcall_version = CATCALL_DISPLAY_VERSION,
    .init            = my_init,
    .push_pixels     = my_push_pixels,
    .fill_rect       = my_fill_rect,
    .set_brightness  = my_set_brightness,
    .get_info        = my_get_info,
    .deinit          = my_deinit,
};

static int driver_init(void) {
    display_config_t cfg = { .landscape = true, .rotation = 0 };
    if (my_init(&cfg) != ESP_OK) return -1;
    purr_kernel_register_display(&s_catcall);
    return 0;
}
static void driver_deinit(void) { my_deinit(); }

purr_module_header_t purr_module = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_DRIVER,
    .name              = "my_display",
    .version           = "0.1.0",
    .kernel_min        = "0.9.0",
    .kernel_max        = "",
    .provided_catcalls = CATCALL_FLAG_DISPLAY,
    .required_catcalls = 0,
    .init              = driver_init,
    .deinit            = driver_deinit,
};
```

**Important:** Always declare `s_catcall` as `static const` at file scope — never as a compound literal passed directly to `purr_kernel_register_display()`. A compound literal is stack-allocated and becomes a dangling pointer as soon as the function returns.

### 4. Build with modulestrap

```bash
python3 modulestrap/modulestrap.py build my_display
# Output: cattobaked/drivers/display/my_display.purr
```

### 5. Reference in device.pcat

```toml
[drivers]
display = "my_display"
```

purrstrap picks it up automatically on the next build.

### Baked-in consumption (specialized kernels)

A specialized kernel (`source/kernel/kernel_<device>/`) consumes the exact same
driver source file — it just calls it directly instead of going through the
`.purr` module loader:

```c
// .purr module path (driver_manager, generic devices):
purr_kernel_load_module("/flash/drivers/touch/gt911.purr");
// → runs gt911.c's own driver_init(), which calls gt911_drv_init()
//   and purr_kernel_register_touch(&s_catcall) internally

// Specialized kernel path (kernel_tdeck_plus/kernel_tdp_boot.c):
#include "../../drivers/touch/gt911/gt911.h"
gt911_configure(sda, scl, int_pin, rst_pin, rotation);
gt911_drv_init();   // same function, same register logic, called directly
```

Both paths end up calling the same `<name>_drv_init()` and registering the same
catcall struct. Never write a second, from-scratch implementation of a driver's
register logic inside a specialized kernel — see
[01_Architecture.md](01_Architecture.md#rule-specialized-kernels-consume-drivers-they-dont-reimplement-them)
for the rule and why it exists.
