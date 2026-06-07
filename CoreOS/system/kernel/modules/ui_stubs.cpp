// ui_stubs.cpp — Placeholder stubs for optional features not yet implemented
// These stubs are only used if their corresponding modules are not compiled in.

#include "esp_log.h"

static const char* TAG = "ui";

// Lua runtime stub (only if lua_runtime.cpp not compiled)
#ifndef PURR_HAS_LUA
void lua_runtime_init() {
    ESP_LOGI(TAG, "Lua runtime: stub (Phase 2 feature)");
}

bool purr_wm_launch(const char* path) {
    (void)path;
    ESP_LOGI(TAG, "App launcher: stub (Phase 2 feature)");
    return false;
}
#endif
