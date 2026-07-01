# PDL Drivers

**PDL** (PURR Driver Language) is a lightweight C-like scripting system for writing hardware drivers that load and unload at runtime from the SD card. No reflash needed — edit the `.drv` file, run `drvmgr reload`, done.

---

## Overview

- `.drv` files live on `/sdcard/drvdebug/`
- Loaded/unloaded via the `drvmgr` shell command over USB serial
- Up to **8 drivers** loaded simultaneously
- Each driver exposes four optional entry points: `init()`, `tick()`, `cmd()`, `deinit()`
- `tick()` is called every ~100ms from the main loop
- Built-in hardware APIs cover GPIO, I2C, ADC, LoRa, delay, logging

---

## Quick example

```c
driver "blink";

int led_pin = 2;
int state = 0;

void init() {
    gpio_mode(led_pin, OUTPUT);
    log("blink: init");
}

void tick() {
    static int ms_last = 0;
    int now = millis();
    if (now - ms_last >= 500) {
        ms_last = now;
        state = !state;
        gpio_write(led_pin, state);
    }
}

void cmd() {
    if (streq(arg(), "on"))  { gpio_write(led_pin, 1); state = 1; }
    if (streq(arg(), "off")) { gpio_write(led_pin, 0); state = 0; }
    log_int(state);
}

void deinit() {
    gpio_write(led_pin, 0);
}
```

Save as `/sdcard/drvdebug/blink.drv`, then from the serial shell:

```
drvmgr load blink
drvmgr cmd blink on
drvmgr cmd blink off
drvmgr unload blink
```

---

## drvmgr shell command

Available whenever `PURR_ENABLE_SHELL=1` (default).

```
drvmgr load <name>          load /sdcard/drvdebug/<name>.drv
                            (absolute path also accepted)
drvmgr unload <name>        call deinit(), free the slot
drvmgr list                 show all loaded drivers
drvmgr cmd <name> [args]    call cmd() with the given argument string
drvmgr tick                 manually fire tick() on all drivers
drvmgr reload <name>        unload + reload from drvdebug in one step
```

---

## Language reference

PDL is a strict C subset. Only `int` and string literals exist as types. No pointers, no structs, no arrays.

### Top-level declarations

```c
driver "name";              // optional — sets the driver's display name

int global_var = 42;        // global integer, accessible in all functions

void init()   { ... }       // called once on load
void tick()   { ... }       // called every ~100ms
void cmd()    { ... }       // called by drvmgr cmd <name> <args>
void deinit() { ... }       // called on unload
int  helper(int x) { ... }  // user-defined function, can return int
```

### Statements

```c
int x = 10;                 // local variable declaration
static int count = 0;       // static local — persists across tick() calls
x = x + 1;
x += 5;
x -= 2;
if (x > 0) { ... }
if (x == 0) { ... } else { ... }
while (x > 0) { x -= 1; }
return x;
```

### Operators

| Operator | Meaning |
|----------|---------|
| `+` `-` `*` `/` `%` | Arithmetic |
| `==` `!=` `<` `>` `<=` `>=` | Comparison |
| `&&` `\|\|` `!` | Logical |
| `=` `+=` `-=` | Assignment |

### Constants

| Name | Value |
|------|-------|
| `INPUT` | 0 |
| `OUTPUT` | 1 |
| `INPUT_PULLUP` | 2 |
| `HIGH` | 1 |
| `LOW` | 0 |

---

## Built-in hardware functions

### GPIO

```c
gpio_mode(pin, mode)        // mode: INPUT=0, OUTPUT=1, INPUT_PULLUP=2
gpio_write(pin, val)        // val: LOW=0 / HIGH=1
int gpio_read(pin)          // returns 0 or 1
```

### ADC

```c
int adc_read(pin)           // 12-bit raw (0–4095) — stub, needs per-pin setup
```

### I2C

```c
i2c_init(sda_pin, scl_pin)  // install I2C master on I2C_NUM_0, 400kHz
int i2c_write(addr, reg, val)   // returns 0=ok, -1=error
int i2c_read(addr, reg)         // returns byte value, -1=error
```

### Timing

```c
delay(ms)                   // block (vTaskDelay)
int millis()                // milliseconds since boot (wraps at ~2B)
```

### Logging

```c
log("message")              // logs string via ESP_LOGI tagged with driver name
log_int(val)                // logs integer value
```

### Command argument

```c
int  arg()                  // returns handle to current cmd() argument string
int  streq(a, b)            // compare two strings — returns 1 if equal
```

Use `arg()` only inside `cmd()`. Pass the result to `streq()`:

```c
void cmd() {
    if (streq(arg(), "reset")) { ... }
}
```

### Radio

```c
// LoRa — only if PURR_HAS_LORA (heltec / tdeck targets)
int lora_send("message")    // returns 0=ok, -1=error
int lora_rssi()             // last packet RSSI in dBm
```

### System

```c
int mem_free()              // free heap bytes
int wifi_connected()        // 1 if connected (stub — returns 0 currently)
```

---

## Static local variables

`static int` inside a function persists its value between calls. Useful for state in `tick()`:

```c
void tick() {
    static int last_ms = 0;
    static int led = 0;
    int now = millis();
    if (now - last_ms >= 1000) {
        last_ms = now;
        led = !led;
        gpio_write(2, led);
    }
}
```

The static value is stored per-driver in the driver manager and survives repeated `tick()` calls for the lifetime of the loaded driver.

---

## Driver lifecycle

```
drvmgr load blink
    │
    ├── read /sdcard/drvdebug/blink.drv
    ├── parse + compile to AST
    ├── call init()
    └── driver is live in a slot

loop (every ~100ms)
    └── call tick() on all loaded drivers

drvmgr cmd blink on
    └── call cmd() with cur_arg = "on"

drvmgr unload blink
    ├── call deinit()
    └── free AST + slot
```

---

## Limitations

- `int` only — no floats, no strings as variables (string literals usable only as `log()` arguments and `streq()` operands)
- No arrays
- No `#include` or multi-file drivers
- I2C always uses `I2C_NUM_0` — conflicts with existing touch drivers on some devices
- `adc_read()` is a stub; requires per-pin ADC oneshot setup
- `wifi_connected()` always returns 0 (stub)
- Maximum 8 drivers loaded at once
- Maximum source file size: 32 KB

---

## Example: I2C sensor read

```c
driver "tmp102";

int addr = 0x48;

void init() {
    i2c_init(21, 22);
    log("tmp102: init");
}

void tick() {
    int raw = i2c_read(addr, 0x00);
    log_int(raw);
}
```

---

## Example: GPIO toggle via cmd

```c
driver "relay";

int pin = 26;

void init() {
    gpio_mode(pin, OUTPUT);
    gpio_write(pin, 0);
}

void cmd() {
    if (streq(arg(), "on"))  { gpio_write(pin, 1); log("relay on");  }
    if (streq(arg(), "off")) { gpio_write(pin, 0); log("relay off"); }
}

void deinit() {
    gpio_write(pin, 0);
}
```

---

## Source files

| File | Role |
|------|------|
| `system/kernel/modules/purr_drv_interp.h/cpp` | Lexer, parser, tree-walk evaluator |
| `system/kernel/modules/purr_drv.h/cpp` | Driver manager (load/tick/unload slots) |
| `components/drv_shell/shell_cmds_drv.cpp` | `drvmgr` shell command implementation |
| `CoreOS/examples/blink.drv` | Minimal example driver |
