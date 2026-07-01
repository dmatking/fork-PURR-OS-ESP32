#pragma once
// purr_script.h — unified PURR OS script dispatch
//
// purr_script_run(path) routes to the correct runtime based on file extension:
//   .lua / .luac → Lua 5.4 (lua_run_file)
//   .py          → MicroPython (mpython_run_file) — requires PURR_HAS_MICROPYTHON
//   .wasm        → WASM (wasm_run_file) — requires PURR_HAS_WASM (future)
//   other / NULL → error

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PURR_SCRIPT_OK       = 0,
    PURR_SCRIPT_ERR_NULL,       // null path
    PURR_SCRIPT_ERR_UNKNOWN,    // unrecognised extension
    PURR_SCRIPT_ERR_RUNTIME,    // runtime not compiled in
    PURR_SCRIPT_ERR_EXEC,       // runtime returned error
} purr_script_result_t;

// Run a script by path. restricted=true blocks dangerous APIs (filesystem writes, OTA).
// Returns PURR_SCRIPT_OK on success.
purr_script_result_t purr_script_run(const char *path, bool restricted);

// Human-readable result string for logging/UI.
const char *purr_script_result_str(purr_script_result_t r);

#ifdef __cplusplus
}
#endif
