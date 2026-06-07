#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Lua Runtime Management ────────────────────────────────────────────────────

// Initialize Lua environment — register all KITT APIs as Lua functions
void lua_runtime_init();

// Load and execute a Lua file (.lua or .luac bytecode)
// Returns true if successful
bool lua_run_file(const char* path, bool restricted);

// Run Lua code as a string
bool lua_run_code(const char* code, const char* name, bool restricted);

// Cleanup
void lua_runtime_deinit();

// ── Lua Module Registration (internal use) ────────────────────────────────────

// Register all display.* functions
void lua_module_display_register();

// Register all sd.* functions
void lua_module_sd_register();

// Register all touch.* functions
void lua_module_touch_register();

// Register all system.* functions (only if !restricted)
void lua_module_system_register(bool restricted);

#ifdef __cplusplus
}
#endif
