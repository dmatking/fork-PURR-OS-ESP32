// ui_stubs.cpp — Placeholder stubs for optional features not yet implemented
// These stubs are only used if their corresponding modules are not compiled in.

#include "esp_log.h"

static const char* TAG = "ui";

// Lua runtime stub (only if lua_runtime.cpp not compiled in)
// purr_wm_launch() is provided by devices/apps/purr_wm_launch.cpp for all builds.
#ifndef PURR_HAS_LUA
void lua_runtime_init() {
    ESP_LOGI(TAG, "Lua runtime: stub (not compiled)");
}
#endif
