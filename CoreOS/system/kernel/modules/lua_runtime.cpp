// lua_runtime.cpp — Lua 5.4 integration for PURR OS (global singleton runtime)
// Used for KITT-level scripting. App windows use app_lua_window.cpp instead.

#include "lua_runtime.h"
#include "../kitt.h"
#ifdef PURR_DISPLAY_ILI9341
#include "display_ili9341.h"
#endif
#ifdef PURR_HAS_CST816S_TOUCH
#include "touch_cst816s.h"
#endif
#include "../purr_idf_compat.h"
#include <lua.hpp>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *LUA_TAG = "lua";
extern KITT kitt;

static lua_State* L = nullptr;
static bool is_initialized = false;

// ── Display Module ───────────────────────────────────────────────────────────

#ifdef PURR_DISPLAY_ILI9341
static int lua_display_fill_rect(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    int h = luaL_checkinteger(L, 4);
    uint32_t color = (uint32_t)luaL_checkinteger(L, 5);

    display_ili9341_fill_rect(x, y, w, h, (uint16_t)color);
    return 0;
}

static int lua_display_draw_hline(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    uint32_t color = (uint32_t)luaL_checkinteger(L, 4);

    display_ili9341_draw_hline(x, y, w, (uint16_t)color);
    return 0;
}

static int lua_display_clear(lua_State* L) {
    (void)L;
    display_ili9341_clear();
    return 0;
}

static int lua_display_text(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    uint32_t fg = (uint32_t)luaL_optinteger(L, 4, 0xFFFF);
    uint32_t bg = (uint32_t)luaL_optinteger(L, 5, 0x0000);
    int sz = (int)luaL_optinteger(L, 6, 1);

    display_ili9341_draw_string(x, y, text, (uint16_t)fg, (uint16_t)bg, (uint8_t)sz);
    return 0;
}
#endif  // PURR_DISPLAY_ILI9341

void lua_module_display_register() {
    if (!L) return;

    lua_newtable(L);  // display table

#ifdef PURR_DISPLAY_ILI9341
    lua_pushcfunction(L, lua_display_fill_rect);
    lua_setfield(L, -2, "fill_rect");

    lua_pushcfunction(L, lua_display_draw_hline);
    lua_setfield(L, -2, "hline");

    lua_pushcfunction(L, lua_display_clear);
    lua_setfield(L, -2, "clear");

    lua_pushcfunction(L, lua_display_text);
    lua_setfield(L, -2, "text");
#endif

    lua_setglobal(L, "display");
}

// ── SD Card Module ───────────────────────────────────────────────────────────

static int lua_sd_read(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);

    FILE* f = fopen(path, "rb");
    if (!f) {
        lua_pushnil(L);
        lua_pushstring(L, "file not found");
        return 2;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(size + 1);
    if (!buf) {
        fclose(f);
        lua_pushnil(L);
        lua_pushstring(L, "out of memory");
        return 2;
    }

    size_t nread = fread(buf, 1, size, f);
    fclose(f);

    buf[nread] = '\0';
    lua_pushlstring(L, buf, nread);
    free(buf);
    return 1;
}

static int lua_sd_write(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    size_t len;
    const char* data = luaL_checklstring(L, 2, &len);

    FILE* f = fopen(path, "wb");
    if (!f) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "cannot open file");
        return 2;
    }

    size_t nwritten = fwrite(data, 1, len, f);
    fclose(f);

    lua_pushboolean(L, nwritten == len);
    return 1;
}

void lua_module_sd_register() {
    if (!L) return;

    lua_newtable(L);  // sd table

    lua_pushcfunction(L, lua_sd_read);
    lua_setfield(L, -2, "read");

    lua_pushcfunction(L, lua_sd_write);
    lua_setfield(L, -2, "write");

    lua_setglobal(L, "sd");
}

// ── Touch Module ──────────────────────────────────────────────────────────────

#ifdef PURR_HAS_CST816S_TOUCH
static int lua_touch_get_event(lua_State* L) {
    cst_touch_event_t ev;
    if (!touch_cst816s_get_event(&ev)) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushinteger(L, ev.x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, ev.y);
    lua_setfield(L, -2, "y");
    lua_pushboolean(L, ev.pressed);
    lua_setfield(L, -2, "pressed");
    lua_pushinteger(L, ev.gesture);
    lua_setfield(L, -2, "gesture");

    return 1;
}
#endif

void lua_module_touch_register() {
    if (!L) return;

    lua_newtable(L);  // touch table

#ifdef PURR_HAS_CST816S_TOUCH
    lua_pushcfunction(L, lua_touch_get_event);
    lua_setfield(L, -2, "get_event");
#endif

    lua_setglobal(L, "touch");
}

// ── System Module (restricted access) ─────────────────────────────────────────

static int lua_system_time_ms(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)(esp_timer_get_time() / 1000LL));
    return 1;
}

static int lua_system_delay(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms < 1 ? 1 : ms));
    return 0;
}

static int lua_system_print(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    ESP_LOGI(LUA_TAG, "%s", s);
    return 0;
}

void lua_module_system_register(bool restricted) {
    if (!L) return;

    lua_newtable(L);  // system table

    lua_pushcfunction(L, lua_system_time_ms);
    lua_setfield(L, -2, "time_ms");

    lua_pushcfunction(L, lua_system_delay);
    lua_setfield(L, -2, "delay");

    lua_pushcfunction(L, lua_system_print);
    lua_setfield(L, -2, "print");

    // Only expose partition/flash APIs if unrestricted (.claw)
    if (!restricted) {
        // Future: add partition_manager, flash APIs here
    }

    lua_setglobal(L, "system");
}

// ── KITT Module ───────────────────────────────────────────────────────────────

static int lua_kitt_log(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    kitt.log("lua", msg);
    return 0;
}

static int lua_kitt_display_width(lua_State *L) {
    lua_pushinteger(L, kitt.display_width());
    return 1;
}

static int lua_kitt_display_height(lua_State *L) {
    lua_pushinteger(L, kitt.display_height());
    return 1;
}

static int lua_kitt_is_ready(lua_State *L) {
    lua_pushboolean(L, kitt.is_ready());
    return 1;
}

static int lua_kitt_text_print(lua_State *L) {
    uint8_t row = (uint8_t)luaL_checkinteger(L, 1);
    const char *text = luaL_checkstring(L, 2);
    kitt.text_print(row, text);
    return 0;
}

static int lua_kitt_text_clear(lua_State *L) {
    (void)L;
    kitt.text_clear();
    return 0;
}

static int lua_kitt_get_touch(lua_State *L) {
    KITT::touch_event_t ev;
    if (!kitt.get_touch_event(&ev)) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, ev.x);     lua_setfield(L, -2, "x");
    lua_pushinteger(L, ev.y);     lua_setfield(L, -2, "y");
    lua_pushboolean(L, ev.pressed); lua_setfield(L, -2, "pressed");
    return 1;
}

void lua_module_kitt_register() {
    if (!L) return;
    lua_newtable(L);
    lua_pushcfunction(L, lua_kitt_log);           lua_setfield(L, -2, "log");
    lua_pushcfunction(L, lua_kitt_display_width);  lua_setfield(L, -2, "display_width");
    lua_pushcfunction(L, lua_kitt_display_height); lua_setfield(L, -2, "display_height");
    lua_pushcfunction(L, lua_kitt_is_ready);       lua_setfield(L, -2, "is_ready");
    lua_pushcfunction(L, lua_kitt_text_print);     lua_setfield(L, -2, "text_print");
    lua_pushcfunction(L, lua_kitt_text_clear);     lua_setfield(L, -2, "text_clear");
    lua_pushcfunction(L, lua_kitt_get_touch);      lua_setfield(L, -2, "get_touch");
    lua_setglobal(L, "kitt");
}

// ── Lua Runtime ──────────────────────────────────────────────────────────────

void lua_runtime_init() {
    if (is_initialized) return;

    L = luaL_newstate();
    if (!L) {
        ESP_LOGE(LUA_TAG, "failed to create Lua state");
        return;
    }

    luaL_openlibs(L);

    // Register KITT API modules
    lua_module_kitt_register();
    lua_module_display_register();
    lua_module_sd_register();
    lua_module_touch_register();
    lua_module_system_register(false);  // default: no restrictions for init

    is_initialized = true;
    ESP_LOGI(LUA_TAG, "runtime initialized");
}

bool lua_run_file(const char* path, bool restricted) {
    if (!L) {
        ESP_LOGE(LUA_TAG, "runtime not initialized");
        return false;
    }

    lua_module_system_register(restricted);
    ESP_LOGI(LUA_TAG, "loading %s (restricted=%d)", path, restricted);

    int status = luaL_loadfile(L, path);
    if (status != LUA_OK) {
        ESP_LOGE(LUA_TAG, "load error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
        ESP_LOGE(LUA_TAG, "runtime error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    return true;
}

bool lua_run_code(const char* code, const char* name, bool restricted) {
    if (!L) {
        ESP_LOGE(LUA_TAG, "runtime not initialized");
        return false;
    }

    lua_module_system_register(restricted);

    int status = luaL_loadbuffer(L, code, strlen(code), name);
    if (status != LUA_OK) {
        ESP_LOGE(LUA_TAG, "compile error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
        ESP_LOGE(LUA_TAG, "runtime error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    return true;
}

void lua_runtime_deinit() {
    if (L) {
        lua_close(L);
        L = nullptr;
    }
    is_initialized = false;
}
