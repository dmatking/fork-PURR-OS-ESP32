# PURR OS — Drivers

## What a Driver Is

In PURR OS, a driver is a `.purr` binary that:
1. Initialises a piece of hardware
2. Registers a catcall struct with the kernel so other modules can use it
3. Handles all hardware-specific details internally

Drivers have no access to each other. They only communicate through the kernel's catcall registry. A display driver never calls a touch driver; it just registers `catcall_display_t` and is done.

---

## driver.pcat Format

Every driver has a `driver.pcat` manifest at `source/drivers/<type>/<name>/driver.pcat`:

```toml
name             = "ili9341"
version          = "0.1.0"
type             = "display"               # display / touch / input / radio / gps
catcall_version  = 1                       # matches CATCALL_DISPLAY_VERSION

idf_min          = "5.3.0"
chip             = ["esp32", "esp32s3"]    # supported chips

kernel_min       = "0.9.0"
kernel_max       = ""                      # empty = no ceiling

module_type      = "PURR_MOD_DRIVER"
provides         = ["catcall_display"]
required_catcalls = []
```

`modulestrap` reads this file to validate before compiling. `purrstrap` uses it to resolve which driver blobs to include in a device build.

---

## Existing Drivers

### Display drivers

#### ili9341

- **Type:** display / SPI
- **Devices:** cyd, cyd_s024c, cyd_s028r
- **Interface:** SPI (MOSI/MISO/CLK/CS/DC/RST/BL)
- **Resolution:** configurable (240×320 or 320×240 depending on MADCTL)
- **Notes:** MADCTL byte `0x40` for correct orientation on s028r variants. DMA mode supported for `push_pixels`.

#### axs15231b

- **Type:** display + touch / QSPI
- **Devices:** jc3248w535
- **Interface:** QSPI 4-bit parallel (CS/PCLK/D0–D3/BL)
- **Resolution:** 480×320
- **Notes:** The display and touch controller are integrated into the same IC. The display driver registers `catcall_display_t`; a companion touch driver registers `catcall_touch_t` via I2C.

#### st7789

- **Type:** display / SPI
- **Devices:** tdeck_plus, waveshare169, tembed_cc1101
- **Interface:** SPI
- **Resolution:** 320×240 (tdeck_plus), 240×280 (waveshare169), 170×320 (tembed)

---

### Touch drivers

#### xpt2046

- **Type:** touch / SPI (shares bus with display)
- **Devices:** cyd (generic)
- **Notes:** Resistive touch. 10-sample median filter for noise reduction. Requires 3-point calibration on first boot (stored in NVS via `catcall_touch_t` and `miniwin_settings`).

#### cst816s

- **Type:** touch / I2C capacitive
- **Devices:** cyd_s024c
- **Notes:** Capacitive, no calibration needed. I2C address 0x15. INT pin optional — driver polls if INT not wired.

#### gt911

- **Type:** touch / I2C capacitive
- **Devices:** tdeck_plus
- **Notes:** Multi-touch capable (reports up to 5 points — only first point used). I2C address 0x5D or 0x14 depending on RST/INT init sequence. INT pin handling is critical — incorrect init causes I2C bus lockup.

---

### Input drivers

#### trackball

- **Type:** input / GPIO
- **Devices:** tdeck_plus
- **Notes:** Four directional GPIO pins (active low) + click button. Generates `INPUT_EVENT_POINTER` events with `delta_x`/`delta_y` = ±1. Cursor acceleration applied at MiniWin layer.

---

### Radio drivers

#### sx1276

- **Type:** radio / SPI
- **Devices:** tdeck_plus
- **Notes:** LoRa radio. SPI bus shared with display on tdeck_plus (separate CS). Supports all standard LoRa spreading factors (SF7–SF12), bandwidths (125/250/500 kHz), and coding rates.

---

### GPS drivers

#### generic_nmea

- **Type:** GPS / UART
- **Devices:** tdeck_plus
- **Notes:** Parses standard NMEA 0183 sentences (GGA, RMC) from any UART-connected GPS module. Runs a background parsing task. GPS probe timeout: 800ms (tuned to avoid blocking boot).

---

## Writing a New Driver

### 1. Create the directory structure

```
source/drivers/<type>/<name>/
  driver.pcat       manifest
  <name>.h          public header (optional — catcall is the interface)
  <name>.c          implementation
```

Or drop it in `user_drivers/<type>/<name>/` for community drivers.

### 2. Write driver.pcat

```toml
name             = "my_display"
version          = "0.1.0"
type             = "display"
catcall_version  = 1
idf_min          = "5.3.0"
chip             = ["esp32", "esp32s3"]
kernel_min       = "0.9.0"
kernel_max       = ""
module_type      = "PURR_MOD_DRIVER"
provides         = ["catcall_display"]
required_catcalls = []
```

### 3. Implement the catcall

```c
// my_display.c

#include "../../kernel/catcalls/catcall_display.h"
#include "../../kernel/core/purr_kernel.h"
#include "../../kernel/core/purr_module.h"

static esp_err_t my_init(const display_config_t *cfg) {
    // initialise hardware here
    return ESP_OK;
}

static esp_err_t my_push_pixels(int x, int y, int w, int h, const uint16_t *data) {
    // write data to display
    return ESP_OK;
}

static esp_err_t my_fill_rect(int x, int y, int w, int h, uint16_t color) {
    // fill rectangle with color
    return ESP_OK;
}

static esp_err_t my_set_brightness(uint8_t level) {
    // set backlight
    return ESP_OK;
}

static void my_get_info(display_info_t *out) {
    out->width  = 320;
    out->height = 240;
    out->bits_per_pixel = 16;
    out->name   = "my_display";
}

static esp_err_t my_deinit(void) {
    return ESP_OK;
}

// ── Catcall struct ────────────────────────────────────────────────────────────

static catcall_display_t s_catcall = {
    .name            = "my_display",
    .catcall_version = CATCALL_DISPLAY_VERSION,
    .init            = my_init,
    .push_pixels     = my_push_pixels,
    .fill_rect       = my_fill_rect,
    .set_brightness  = my_set_brightness,
    .get_info        = my_get_info,
    .deinit          = my_deinit,
};

// ── Module init (called by driver_manager) ────────────────────────────────────

static int driver_init(void) {
    display_config_t cfg = { .landscape = true, .rotation = 0 };
    if (my_init(&cfg) != ESP_OK) return -1;
    purr_kernel_register_display(&s_catcall);
    return 0;
}

static void driver_deinit(void) {
    my_deinit();
}

// ── .purr module header ───────────────────────────────────────────────────────

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

### 4. Build with modulestrap

```bash
# If in user_drivers/:
python3 modulestrap/modulestrap.py build my_display

# Output: cattobaked/drivers/display/my_display.purr
```

### 5. Reference in device.pcat

```toml
[drivers]
display = "my_display"
```

That's all. `purrstrap` will pick it up automatically.

---

## Driver Compat Matrix

| Driver | ESP32 | ESP32-S3 | kernel_min |
|--------|-------|----------|-----------|
| ili9341 | ✓ | ✓ | 0.9.0 |
| axs15231b | — | ✓ | 0.9.0 |
| st7789 | — | ✓ | 0.9.0 |
| xpt2046 | ✓ | ✓ | 0.9.0 |
| cst816s | ✓ | ✓ | 0.9.0 |
| gt911 | — | ✓ | 0.9.0 |
| trackball | — | ✓ | 0.9.0 |
| sx1276 | — | ✓ | 0.9.0 |
| generic_nmea | ✓ | ✓ | 0.9.0 |
