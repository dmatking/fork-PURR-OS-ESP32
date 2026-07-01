#pragma once
#include <stdint.h>
#include <stdbool.h>

// PURR Driver Language (.drv) interpreter
// Subset of C: int vars, if/else, while, function calls, built-in hardware APIs.
//
// Driver entry points (called by purr_drv manager):
//   void init()        — called once on load
//   void tick()        — called every ~100ms
//   void cmd()         — called by drvmgr cmd <name> <args>; use arg()/streq()
//   void deinit()      — called on unload
//
// Built-in functions:
//   gpio_mode(pin, mode)        mode: 0=INPUT 1=OUTPUT 2=INPUT_PULLUP
//   gpio_read(pin) -> int
//   gpio_write(pin, val)
//   adc_read(pin) -> int        12-bit, 0-4095
//   i2c_init(sda, scl)
//   i2c_write(addr, reg, val) -> int   0=ok
//   i2c_read(addr, reg) -> int         -1=err
//   delay(ms)
//   millis() -> int
//   mem_free() -> int           free heap bytes
//   log(str_literal)
//   log_int(val)
//   streq(a, b) -> int          compare two strings (from arg() or literals)
//   arg() -> int (opaque)       current cmd() argument string handle
//   lora_send(str_literal) -> int      (only if PURR_HAS_LORA)
//   lora_rssi() -> int
//   wifi_connected() -> int

typedef struct pdrv_script_t pdrv_script_t;

// Parse .drv source. Returns NULL on error (err_buf filled).
pdrv_script_t *pdrv_parse(const char *src, char *err_buf, int err_len);
void           pdrv_free(pdrv_script_t *s);
const char    *pdrv_name(pdrv_script_t *s);

// Call a named function. arg_str is the command argument (for cmd()).
// Returns false on runtime error.
bool pdrv_call(pdrv_script_t *s, const char *func, const char *arg_str,
               char *err_buf, int err_len);
