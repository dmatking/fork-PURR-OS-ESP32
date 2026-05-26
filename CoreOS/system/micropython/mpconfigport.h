// Minimal MicroPython port config for PURR OS CoreOS embed.
// Place this file where the MicroPython build can find it, or point
// MICROPY_CONFIG_FILE at it in your CMake/make invocation.

#include <stdint.h>

// Use the standard embed port defaults as a base
#include "port/mpconfigport_minimal.h"

// Register the built-in kitt module so `import kitt` works without SPIFFS lookup
extern const struct _mp_obj_module_t kitt_module_obj;
#define MICROPY_PORT_BUILTIN_MODULES \
    { MP_ROM_QSTR(MP_QSTR_kitt), MP_ROM_PTR(&kitt_module_obj) },

// Heap size is set at runtime via mp_embed_init() — see mpython_runtime.cpp
// Default stack for MicroPython tasks
#define MICROPY_STACK_CHECK          (1)
#define MICROPY_ENABLE_GC            (1)
#define MICROPY_HELPER_REPL          (0)
#define MICROPY_MODULE_BUILTIN_INIT  (1)

// utime shim — map utime.ticks_ms() to millis()
#define MICROPY_PY_UTIME_MP_HAL      (1)
