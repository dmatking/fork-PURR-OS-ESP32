// lua_runtime.cpp — Lua 5.4 integration for PURR OS
// Provides Lua environment with sandboxed access to KITT APIs.
// Apps can be restricted (.paws) or full-access (.claw).

#include "lua_runtime.h"
#include "../kitt.h"
#include "display_ili9341.h"
#include "touch_cst816s.h"
#include "../purr_idf_compat.h"
#include <lua.hpp>
#include <stdio.h>
#include <string.h>

extern KITT kitt;

static lua_State* L = nullptr;
static bool is_initialized = false;

// ── Display Module ───────────────────────────────────────────────────────────

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

void lua_module_display_register() {
    if (!L) return;

    lua_newtable(L);  // display table

    lua_pushcfunction(L, lua_display_fill_rect);
    lua_setfield(L, -2, "fill_rect");

    lua_pushcfunction(L, lua_display_draw_hline);
    lua_setfield(L, -2, "hline");

    lua_pushcfunction(L, lua_display_clear);
    lua_setfield(L, -2, "clear");

    lua_pushcfunction(L, lua_display_text);
    lua_setfield(L, -2, "text");

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

void lua_module_touch_register() {
    if (!L) return;

    lua_newtable(L);  // touch table

    lua_pushcfunction(L, lua_touch_get_event);
    lua_setfield(L, -2, "get_event");

    lua_setglobal(L, "touch");
}

// ── System Module (restricted access) ─────────────────────────────────────────

static int lua_system_time_ms(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)millis());
    return 1;
}

static int lua_system_delay(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
    delay(ms);
    return 0;
}

static int lua_system_print(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    Serial.print(s);
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

// ── Lua Runtime ──────────────────────────────────────────────────────────────

void lua_runtime_init() {
    if (is_initialized) return;

    L = luaL_newstate();
    if (!L) {
        Serial.println("[lua] ERROR: failed to create Lua state");
        return;
    }

    luaL_openlibs(L);

    // Register KITT API modules
    lua_module_display_register();
    lua_module_sd_register();
    lua_module_touch_register();
    lua_module_system_register(false);  // default: no restrictions for init

    is_initialized = true;
    Serial.println("[lua] runtime initialized");
}

bool lua_run_file(const char* path, bool restricted) {
    if (!L) {
        Serial.println("[lua] ERROR: runtime not initialized");
        return false;
    }

    // Re-register system module with correct restriction level
    lua_module_system_register(restricted);

    Serial.printf("[lua] loading %s (restricted=%d)\n", path, restricted);

    int status = luaL_loadfile(L, path);
    if (status != LUA_OK) {
        Serial.printf("[lua] load error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
        Serial.printf("[lua] runtime error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    return true;
}

bool lua_run_code(const char* code, const char* name, bool restricted) {
    if (!L) {
        Serial.println("[lua] ERROR: runtime not initialized");
        return false;
    }

    lua_module_system_register(restricted);

    int status = luaL_loadbuffer(L, code, strlen(code), name);
    if (status != LUA_OK) {
        Serial.printf("[lua] compile error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
        Serial.printf("[lua] runtime error: %s\n", lua_tostring(L, -1));
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
