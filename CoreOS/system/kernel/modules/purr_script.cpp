// purr_script.cpp — unified PURR OS script dispatch

#include "purr_script.h"
#include "lua_runtime.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "script";

#ifdef PURR_HAS_MICROPYTHON
#  include "mpython_runtime.h"
#endif

static const char *ext_of(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot ? dot : "";
}

purr_script_result_t purr_script_run(const char *path, bool restricted) {
    if (!path) return PURR_SCRIPT_ERR_NULL;

    const char *ext = ext_of(path);

    if (strcmp(ext, ".lua") == 0 || strcmp(ext, ".luac") == 0 ||
        strcmp(ext, ".paws") == 0 || strcmp(ext, ".claw") == 0) {
#ifdef PURR_HAS_LUA
        ESP_LOGI(TAG, "lua: %s", path);
        bool ok = lua_run_file(path, restricted);
        return ok ? PURR_SCRIPT_OK : PURR_SCRIPT_ERR_EXEC;
#else
        ESP_LOGW(TAG, "Lua not compiled in — PURR_HAS_LUA=0");
        return PURR_SCRIPT_ERR_RUNTIME;
#endif
    }

    if (strcmp(ext, ".py") == 0) {
#ifdef PURR_HAS_MICROPYTHON
        ESP_LOGI(TAG, "micropython: %s", path);
        bool ok = mpython_run_file(path);
        return ok ? PURR_SCRIPT_OK : PURR_SCRIPT_ERR_EXEC;
#else
        ESP_LOGW(TAG, "MicroPython not compiled in — PURR_HAS_MICROPYTHON=0");
        return PURR_SCRIPT_ERR_RUNTIME;
#endif
    }

    if (strcmp(ext, ".wasm") == 0) {
        ESP_LOGW(TAG, "WASM runtime not yet implemented");
        return PURR_SCRIPT_ERR_RUNTIME;
    }

    ESP_LOGW(TAG, "unknown script extension: '%s' (path=%s)", ext, path);
    return PURR_SCRIPT_ERR_UNKNOWN;
}

const char *purr_script_result_str(purr_script_result_t r) {
    switch (r) {
    case PURR_SCRIPT_OK:          return "OK";
    case PURR_SCRIPT_ERR_NULL:    return "null path";
    case PURR_SCRIPT_ERR_UNKNOWN: return "unknown extension";
    case PURR_SCRIPT_ERR_RUNTIME: return "runtime not available";
    case PURR_SCRIPT_ERR_EXEC:    return "execution error";
    default:                       return "unknown error";
    }
}
