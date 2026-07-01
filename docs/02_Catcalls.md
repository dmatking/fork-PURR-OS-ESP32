# PURR OS — Catcalls

Catcalls are PURR OS's version of syscalls. They are the only way any module or app communicates with hardware — nothing ever calls a driver function directly. The kernel holds one registered implementation per catcall type. Drivers register; everyone else calls through the kernel accessor.

**Pattern:**
```c
// Driver registers its implementation once at init:
purr_kernel_register_display(&my_ili9341_catcall);

// Everything else reads through the kernel — never calls the driver directly:
const catcall_display_t *disp = purr_kernel_display();
if (disp) disp->fill_rect(0, 0, 320, 240, 0x0000);
```

All catcalls follow this pattern. Always null-check the accessor — if no driver has registered, it returns `NULL`.

---

## Catcall Registry

| Catcall | Flag constant | Register fn | Accessor fn |
|---------|--------------|-------------|-------------|
| display | `CATCALL_FLAG_DISPLAY` (`1<<0`) | `purr_kernel_register_display()` | `purr_kernel_display()` |
| touch   | `CATCALL_FLAG_TOUCH` (`1<<1`)   | `purr_kernel_register_touch()` | `purr_kernel_touch()` |
| input   | `CATCALL_FLAG_INPUT` (`1<<2`)   | `purr_kernel_register_input()` | `purr_kernel_input()` |
| radio   | `CATCALL_FLAG_RADIO` (`1<<3`)   | `purr_kernel_register_radio()` | `purr_kernel_radio()` |
| gps     | `CATCALL_FLAG_GPS` (`1<<4`)     | `purr_kernel_register_gps()` | `purr_kernel_gps()` |
| ui      | `CATCALL_FLAG_UI` (`1<<5`)      | `purr_kernel_register_ui()` | `purr_kernel_ui()` |

Each slot holds exactly one registered implementation. A second driver attempting to register the same catcall is silently ignored — first one wins. This matches hardware reality: there is one display.

---

## catcall_display_t

**Source:** `source/kernel/catcalls/catcall_display.h`
**Flag:** `CATCALL_FLAG_DISPLAY` (`1<<0`)
**Version:** `CATCALL_DISPLAY_VERSION 1`

The display catcall is required for all visual modules. KittenUI and MiniWin will refuse to start without it.

```c
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  bits_per_pixel;    // always 16 (RGB565) in current drivers
    const char *name;           // e.g. "ILI9341"
} display_info_t;

typedef struct {
    bool     landscape;
    uint8_t  rotation;          // 0/1/2/3
} display_config_t;

typedef struct {
    const char  *name;
    uint8_t      catcall_version;

    esp_err_t  (*init)(const display_config_t *cfg);
    esp_err_t  (*push_pixels)(int x, int y, int w, int h, const uint16_t *data);
    esp_err_t  (*fill_rect)(int x, int y, int w, int h, uint16_t color);
    esp_err_t  (*set_brightness)(uint8_t level);   // 0-255
    void       (*get_info)(display_info_t *out);
    esp_err_t  (*deinit)(void);
} catcall_display_t;
```

### Functions

| Function | Description |
|----------|-------------|
| `init(cfg)` | Initialise display hardware with orientation from device.pcat. Called by driver_manager. |
| `push_pixels(x,y,w,h,data)` | Blit a rectangle of RGB565 pixels. `data` is `w*h` uint16_t values, row-major. |
| `fill_rect(x,y,w,h,color)` | Fill a rectangle with a single RGB565 colour. Faster than push_pixels for solid fills. |
| `set_brightness(level)` | Set backlight brightness 0-255. No-op on drivers without PWM. |
| `get_info(out)` | Fill `display_info_t` with dimensions and capabilities. |
| `deinit()` | Shut down display hardware. |

### RGB565 format

All pixel data is RGB565, big-endian (byte-swapped for direct SPI DMA):
```
bits 15-11: red (5 bits)
bits 10-5:  green (6 bits)
bits 4-0:   blue (5 bits)
```

### Existing display drivers

| Slug | Panel | Interface | Devices |
|------|-------|-----------|---------|
| `ili9341` | ILI9341 320x240 | SPI + DMA | cyd, cyd_s024c, cyd_s028r |
| `st7789` | ST7789 320x240 | SPI + DMA | tdeck, tdeck_plus |
| `axs15231b` | AXS15231B 480x320 | QSPI | jc3248w535 |
| `ssd1306` | SSD1306 128x64 | I2C | heltec |

---

## catcall_touch_t

**Source:** `source/kernel/catcalls/catcall_touch.h`
**Flag:** `CATCALL_FLAG_TOUCH` (`1<<1`)
**Version:** `CATCALL_TOUCH_VERSION 1`

```c
typedef struct {
    uint8_t  i2c_port;
    uint8_t  sda_pin;
    uint8_t  scl_pin;
    uint8_t  int_pin;    // 0xFF = no interrupt pin
    uint8_t  rst_pin;    // 0xFF = no reset pin
} touch_config_t;

typedef struct {
    const char *name;
    uint8_t     catcall_version;

    esp_err_t (*init)(const touch_config_t *cfg);
    bool      (*read_point)(uint16_t *x, uint16_t *y);
    bool      (*is_pressed)(void);
    esp_err_t (*deinit)(void);
} catcall_touch_t;
```

### Functions

| Function | Description |
|----------|-------------|
| `init(cfg)` | Initialise touch controller with I2C/pin config from device.pcat. |
| `read_point(x,y)` | Read current touch point in screen pixels. Returns `true` if a touch is active. Non-blocking. |
| `is_pressed()` | Returns `true` if currently touched. Cheaper than `read_point` when coordinates are not needed. |
| `deinit()` | Shut down touch controller. |

### Notes

- Coordinates are already mapped to screen pixels (no raw digitiser values).
- XPT2046 (resistive) applies an internal 3-sample median filter.
- CST816S, GT911, AXS15231B (capacitive) read directly from the IC register.

### Existing touch drivers

| Slug | Type | Interface | Devices |
|------|------|-----------|---------|
| `xpt2046` | Resistive | SPI (shared bus) | cyd, cyd_s028r |
| `cst816s` | Capacitive | I2C | cyd_s024c, waveshare169 |
| `gt911` | Capacitive | I2C | tdeck_plus |
| `axs15231b` | Capacitive | I2C | jc3248w535 |

---

## catcall_input_t

**Source:** `source/kernel/catcalls/catcall_input.h`
**Flag:** `CATCALL_FLAG_INPUT` (`1<<2`)
**Version:** `CATCALL_INPUT_VERSION 1`

Handles non-touch input: keyboards, trackballs, rotary encoders.

```c
typedef enum {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEY_DOWN,
    INPUT_EVENT_KEY_UP,
    INPUT_EVENT_POINTER,    // trackball / mouse delta
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    uint16_t keycode;       // USB HID keycode for KEY_DOWN/UP
    int16_t  delta_x;       // trackball delta for POINTER
    int16_t  delta_y;
    uint8_t  modifiers;     // shift/ctrl/alt bitmask
} input_event_t;

typedef struct {
    const char *name;
    uint8_t     catcall_version;

    esp_err_t (*init)(void);
    bool      (*poll_event)(input_event_t *out);
    esp_err_t (*deinit)(void);
} catcall_input_t;
```

### Functions

| Function | Description |
|----------|-------------|
| `init()` | Initialise input hardware (keyboard I2C, trackball GPIOs, etc.). |
| `poll_event(out)` | Non-blocking poll. Returns `true` and fills `out` if an event is pending. Returns `false` if queue is empty. |
| `deinit()` | Shut down input hardware. |

### Keycode convention

Key events use USB HID keycodes. `modifiers` bitmask:
```
bit 0 - left ctrl     bit 4 - right ctrl
bit 1 - left shift    bit 5 - right shift
bit 2 - left alt      bit 6 - right alt
bit 3 - left GUI      bit 7 - right GUI
```

### Existing input drivers

| Slug | Type | Devices |
|------|------|---------|
| `trackball` | 4-dir GPIO trackball | tdeck, tdeck_plus |

---

## catcall_radio_t

**Source:** `source/kernel/catcalls/catcall_radio.h`
**Flag:** `CATCALL_FLAG_RADIO` (`1<<3`)
**Version:** `CATCALL_RADIO_VERSION 1`

This catcall covers SPI LoRa modules (SX1262, SX1276). Built-in WiFi and Bluetooth use ESP-IDF APIs directly.

```c
typedef struct {
    uint32_t frequency_hz;
    int8_t   tx_power_dbm;
    uint8_t  spreading_factor;   // LoRa: 7-12
    uint32_t bandwidth_hz;       // LoRa: 125000/250000/500000
    uint8_t  coding_rate;        // LoRa: 5-8 (denominator of 4/x)
} radio_config_t;

typedef struct {
    const char *name;
    uint8_t     catcall_version;

    esp_err_t (*init)(const radio_config_t *cfg);
    esp_err_t (*send)(const uint8_t *data, size_t len);
    int       (*receive)(uint8_t *buf, size_t max_len);
    bool      (*data_available)(void);
    int8_t    (*rssi)(void);
    float     (*snr)(void);
    esp_err_t (*set_frequency)(uint32_t hz);
    esp_err_t (*set_power)(int8_t dbm);
    esp_err_t (*deinit)(void);
} catcall_radio_t;
```

### Functions

| Function | Description |
|----------|-------------|
| `init(cfg)` | Initialise radio with frequency, spreading factor, bandwidth, coding rate. |
| `send(data,len)` | Transmit a packet. Blocks until TX complete. |
| `receive(buf,max)` | Copy received packet into buf. Returns byte count, or -1 if nothing waiting. Non-blocking. |
| `data_available()` | Returns `true` if a received packet is in the driver buffer. |
| `rssi()` | Signal strength of the last received packet in dBm. |
| `snr()` | Signal-to-noise ratio of the last received packet. |
| `set_frequency(hz)` | Change operating frequency on the fly. |
| `set_power(dbm)` | Change TX power on the fly. |
| `deinit()` | Power down the radio. |

### Built-in WiFi / Bluetooth

WiFi and BT are silicon peripherals, not exposed through `catcall_radio_t`. The `[radio]` section of `device.pcat` declares their presence:

```ini
[radio]
wifi = true
bt   = true
lora = "sx1276"
```

purrstrap emits `CONFIG_PURR_WIFI`, `CONFIG_PURR_BT`, `CONFIG_PURR_LORA`, and `CONFIG_PURR_LORA_DRIVER` into the device glue layer for conditional compilation in kernel code.

### Existing LoRa drivers

| Slug | Chip | Devices |
|------|------|---------|
| `sx1262` | SX1262 | heltec, tdeck |
| `sx1276` | SX1276 | tdeck_plus |

---

## catcall_gps_t

**Source:** `source/kernel/catcalls/catcall_gps.h`
**Flag:** `CATCALL_FLAG_GPS` (`1<<4`)
**Version:** `CATCALL_GPS_VERSION 1`

```c
typedef struct {
    double   latitude;     // decimal degrees, positive = north
    double   longitude;    // decimal degrees, positive = east
    float    altitude_m;
    float    speed_mps;
    float    hdop;         // horizontal dilution of precision
    uint8_t  satellites;   // satellites used in fix
    bool     valid;        // false until first fix acquired
} gps_fix_t;

typedef struct {
    const char *name;
    uint8_t     catcall_version;

    esp_err_t (*init)(void);
    bool      (*get_fix)(gps_fix_t *out);
    esp_err_t (*deinit)(void);
} catcall_gps_t;
```

### Functions

| Function | Description |
|----------|-------------|
| `init()` | Start the GPS UART + NMEA parser background task. Non-blocking. |
| `get_fix(out)` | Fill `out` with the latest GPS data. Returns `true` if `out->valid` is set (fix acquired). |
| `deinit()` | Stop the GPS task and UART. |

### Existing GPS drivers

| Slug | Protocol | NMEA sentences | Devices |
|------|----------|----------------|---------|
| `generic_nmea` | UART 9600 baud | $GPRMC, $GPGGA | tdeck_plus |

---

## catcall_ui_t  *(new in v0.12.0)*

**Source:** `source/kernel/catcalls/catcall_ui.h`
**Flag:** `CATCALL_FLAG_UI` (`1<<5`)
**Version:** `CATCALL_UI_VERSION 1`

The UI catcall is the widget/windowing abstraction layer added in v0.12.0. UI modules (KittenUI, MiniWin, oled_ui) register an implementation at boot; all apps call through `purr_win.h` which dispatches to the registered backend. Apps never touch LVGL or MiniWin APIs directly, making them portable across all display/UI combinations.

```c
typedef uint32_t purr_win_t;   // opaque window handle
typedef uint32_t purr_wid_t;   // opaque widget handle

typedef enum {
    PURR_EVENT_CLICKED = 0,
    PURR_EVENT_CHANGED,
    PURR_EVENT_FOCUSED,
} purr_event_t;

typedef void (*purr_win_cb_t)(purr_wid_t wid, purr_event_t event, void *user);

typedef enum { PURR_ALIGN_LEFT=0, PURR_ALIGN_CENTER, PURR_ALIGN_RIGHT } purr_align_t;
typedef enum { PURR_LAYOUT_ROW=0, PURR_LAYOUT_COL } purr_layout_t;

typedef struct {
    const char *name;
    uint8_t     catcall_version;

    // Windows
    purr_win_t (*win_create) (const char *title);
    void       (*win_destroy)(purr_win_t win);
    void       (*win_show)   (purr_win_t win);
    void       (*win_hide)   (purr_win_t win);
    void       (*win_clear)  (purr_win_t win);

    // Labels
    purr_wid_t (*label_create)(purr_win_t win, const char *text);
    void       (*label_set)   (purr_wid_t wid, const char *text);
    void       (*label_align) (purr_wid_t wid, purr_align_t align);

    // Buttons
    purr_wid_t (*btn_create)(purr_win_t win, const char *label,
                              purr_win_cb_t cb, void *user);
    void       (*btn_enable)(purr_wid_t wid, bool enabled);

    // Textarea
    purr_wid_t    (*textarea_create)   (purr_win_t win, uint16_t w_pct, uint16_t h_pct);
    void          (*textarea_append)   (purr_wid_t wid, const char *text);
    void          (*textarea_set)      (purr_wid_t wid, const char *text);
    void          (*textarea_clear)    (purr_wid_t wid);
    const char   *(*textarea_get)      (purr_wid_t wid);
    void          (*textarea_focus)    (purr_wid_t wid);
    void          (*textarea_on_change)(purr_wid_t wid, purr_win_cb_t cb, void *user);

    // Layout containers
    purr_wid_t (*layout_begin)(purr_win_t win, purr_layout_t type, uint8_t padding);
    void       (*layout_end)  (purr_wid_t container);

    // On-screen keyboard
    void (*kb_show)(purr_win_t win, purr_wid_t target);
    void (*kb_hide)(purr_win_t win);
} catcall_ui_t;
```

### Registered implementations

| UI module | Backend | Devices | Notes |
|-----------|---------|---------|-------|
| `kittenui` | LVGL 8.x | cyd*, tdeck* | Small-to-large SPI screens |
| `miniwin` | MiniWin WM | jc3248w535 | Large QSPI screen (480x320) |
| `oled_ui` | Custom text renderer | heltec | 128x64 text-mode only |

### Using the UI catcall from an app

Apps include `purr_win.h` — not `catcall_ui.h`:

```c
#include "purr_win.h"

static purr_win_t s_win;

static void on_tap(purr_wid_t w, purr_event_t e, void *u) {
    purr_win_label_set((purr_wid_t)(uintptr_t)u, "Tapped!");
}

int my_app_init(void) {
    s_win = purr_win_create("Demo");
    purr_wid_t lbl = purr_win_label(s_win, "Hello, PURR OS!");
    purr_win_button(s_win, "Tap me", on_tap, (void*)(uintptr_t)lbl);
    purr_win_show(s_win);
    return 0;
}
```

`purr_win.h` null-checks the registered backend before every call — the app silently no-ops if no UI module is loaded yet.

### Writing a new UI backend

Implement all function pointers in `catcall_ui_t`, then call `purr_kernel_register_ui(&my_ui)` from your module's `init()`. Any pointer left `NULL` becomes a graceful no-op. See `source/modules/kittenui/kittenui_win.c` and `source/modules/miniwin/miniwin_win.c` for reference implementations.

---

## Glue Layer — How Pins Get to Drivers

Drivers do not hardcode pin numbers. purrstrap generates `purr_device_glue.c` for each device with `#define` macros from `device.pcat [pins]`:

```c
#define CONFIG_DRV_DISPLAY_CS_PIN  15
#define CONFIG_DRV_DISPLAY_DC_PIN  2
#define CONFIG_DRV_DISPLAY_BL_PIN  27
#define CONFIG_PURR_WIFI           1
#define CONFIG_PURR_BT             1
#define CONFIG_PURR_LORA           1
#define CONFIG_PURR_LORA_DRIVER    "sx1276"

const char *purr_flash_app_dir = "/flash/apps";
const char *purr_sd_app_dir    = "/sdcard/apps";
```

Drivers `#include` these defines — the same driver binary runs on any device that provides its catcall hardware.

## Implementing a New Catcall (Driver Author)

1. Implement all function pointers in the catcall struct (leave unused ones as `NULL`).
2. Define a static const instance of the struct.
3. In your module's `init()`, call the matching `purr_kernel_register_*()`.
4. Set `provided_catcalls` bitmask in your `purr_module_header_t`.

```c
static const catcall_display_t s_disp = {
    .name            = "my_display",
    .catcall_version = CATCALL_DISPLAY_VERSION,
    .init            = my_init,
    .push_pixels     = my_push_pixels,
    .fill_rect       = my_fill_rect,
    .set_brightness  = NULL,   // not supported on this panel
    .get_info        = my_get_info,
    .deinit          = my_deinit,
};

static int my_module_init(void) {
    purr_kernel_register_display(&s_disp);
    return 0;
}

purr_module_header_t purr_module = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_DRIVER,
    .load_priority     = PURR_PRIORITY_REQUIRED,
    .name              = "my_display",
    .provided_catcalls = CATCALL_FLAG_DISPLAY,
    .required_catcalls = 0,
    .init              = my_module_init,
    .deinit            = my_module_deinit,
};
```
