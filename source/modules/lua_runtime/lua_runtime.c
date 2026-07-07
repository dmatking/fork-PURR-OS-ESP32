// lua_runtime.c — Lua 5.4 scripting runtime for PURR OS (.meow/.hiss apps)
//
// One global Lua state, matching PURR-OS-0.11's design (only one Lua VM
// runs at a time on these boards) — adapted from that version's
// lua_runtime.cpp, but plain C and bound to purr_win.h/purr_kernel.h
// instead of KITT's C++ singleton + hardware-specific display/touch calls.
//
// Threading model: a .meow script's "main body" runs synchronously inside
// meow_task() (app_manager.c), which self-deletes once init() (this file's
// lua_runtime_init()) returns — so by the time any win.* button callback
// can fire (dispatched from the UI backend's own render/pump task), the
// script's own task is already gone. Scripts that build a UI and return
// (rather than looping) never have two tasks touching the single lua_State
// at once; this runtime does not support scripts that both loop forever
// AND register button callbacks, since the UI task's callback trampoline
// isn't synchronized against a running lua_pcall() on another task.

#include "lua_runtime.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"
#include "app_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "lua_rt";

static lua_State *s_L = NULL;

// ── system.* ──────────────────────────────────────────────────────────────────

static int lua_system_time_ms(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)(esp_timer_get_time() / 1000LL));
    return 1;
}

static int lua_system_delay(lua_State *L) {
    lua_Integer ms = luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms < 1 ? 1 : (uint32_t)ms));
    return 0;
}

static int lua_system_print(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    ESP_LOGI(TAG, "%s", s);
    return 0;
}

static void register_system_module(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, lua_system_time_ms); lua_setfield(L, -2, "time_ms");
    lua_pushcfunction(L, lua_system_delay);   lua_setfield(L, -2, "delay");
    lua_pushcfunction(L, lua_system_print);   lua_setfield(L, -2, "print");
    lua_setglobal(L, "system");
}

// ── sd.* ──────────────────────────────────────────────────────────────────────
// Works against /flash or /sdcard like any other PURR OS app — no separate
// "restricted" tier distinction here (that's app_manager.c's .meow/.paws/
// .claw file-extension tiering, applied at the file-manager/app-scan level,
// not re-implemented inside the VM).

static int lua_sd_read(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    FILE *f = fopen(path, "rb");
    if (!f) { lua_pushnil(L); lua_pushstring(L, "file not found"); return 2; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); lua_pushnil(L); lua_pushstring(L, "stat failed"); return 2; }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); lua_pushnil(L); lua_pushstring(L, "out of memory"); return 2; }

    size_t nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    lua_pushlstring(L, buf, nread);
    free(buf);
    return 1;
}

static int lua_sd_write(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);

    FILE *f = fopen(path, "wb");
    if (!f) { lua_pushboolean(L, 0); lua_pushstring(L, "cannot open file"); return 2; }
    size_t nwritten = fwrite(data, 1, len, f);
    fclose(f);
    lua_pushboolean(L, nwritten == len);
    return 1;
}

static void register_sd_module(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, lua_sd_read);  lua_setfield(L, -2, "read");
    lua_pushcfunction(L, lua_sd_write); lua_setfield(L, -2, "write");
    lua_setglobal(L, "sd");
}

// ── radio.* / gps.* / kitt.* — .hiss tier only ────────────────────────────────
// Registered by register_kitt_module() below, called from lua_runtime_init()
// only when app_manager_get_pending_meow_privileged() is true. A plain
// .meow script never sees these globals at all — not hidden, just never
// added to its Lua state.

static int lua_radio_send(lua_State *L) {
    size_t len;
    const char *data = luaL_checklstring(L, 1, &len);
    const catcall_radio_t *radio = purr_kernel_radio();
    if (!radio || !radio->send) { lua_pushboolean(L, 0); lua_pushstring(L, "no radio"); return 2; }
    esp_err_t ret = radio->send((const uint8_t *)data, len);
    lua_pushboolean(L, ret == ESP_OK);
    return 1;
}

static int lua_radio_receive(lua_State *L) {
    lua_Integer max_len = luaL_optinteger(L, 1, 256);
    if (max_len <= 0 || max_len > 1024) max_len = 256;
    const catcall_radio_t *radio = purr_kernel_radio();
    if (!radio || !radio->receive) { lua_pushnil(L); return 1; }
    uint8_t buf[1024];
    int n = radio->receive(buf, (size_t)max_len);
    if (n <= 0) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char *)buf, (size_t)n);
    return 1;
}

static int lua_radio_available(lua_State *L) {
    const catcall_radio_t *radio = purr_kernel_radio();
    lua_pushboolean(L, radio && radio->data_available && radio->data_available());
    return 1;
}

static int lua_radio_rssi(lua_State *L) {
    const catcall_radio_t *radio = purr_kernel_radio();
    lua_pushinteger(L, (radio && radio->rssi) ? radio->rssi() : 0);
    return 1;
}

static int lua_radio_snr(lua_State *L) {
    const catcall_radio_t *radio = purr_kernel_radio();
    lua_pushnumber(L, (radio && radio->snr) ? (lua_Number)radio->snr() : 0.0);
    return 1;
}

static int lua_gps_fix(lua_State *L) {
    const catcall_gps_t *gps = purr_kernel_gps();
    if (!gps || !gps->get_fix) { lua_pushnil(L); return 1; }
    gps_fix_t fix = {0};
    bool has_fix = gps->get_fix(&fix);
    lua_newtable(L);
    lua_pushnumber(L,  fix.latitude);           lua_setfield(L, -2, "latitude");
    lua_pushnumber(L,  fix.longitude);          lua_setfield(L, -2, "longitude");
    lua_pushnumber(L,  fix.altitude_m);         lua_setfield(L, -2, "altitude_m");
    lua_pushnumber(L,  fix.speed_mps);          lua_setfield(L, -2, "speed_mps");
    lua_pushnumber(L,  fix.hdop);                lua_setfield(L, -2, "hdop");
    lua_pushinteger(L, fix.satellites);          lua_setfield(L, -2, "satellites");
    lua_pushboolean(L, has_fix && fix.valid);    lua_setfield(L, -2, "valid");
    return 1;
}

// Mirrors terminal.c's module_type_name() — small enough not to be worth
// sharing a header over.
static const char *lua_module_type_name(uint8_t type) {
    switch (type) {
        case PURR_MOD_DRIVER: return "driver";
        case PURR_MOD_SYSTEM: return "system";
        case PURR_MOD_UI:     return "ui";
        case PURR_MOD_APP:    return "app";
        default:              return "unknown";
    }
}

static int lua_kitt_modules(lua_State *L) {
    int n = purr_kernel_module_count();
    lua_newtable(L);
    int out_idx = 1;
    for (int i = 0; i < n; i++) {
        const purr_module_header_t *hdr = purr_kernel_module_at(i);
        if (!hdr) continue;
        lua_newtable(L);
        lua_pushstring(L, hdr->name);                          lua_setfield(L, -2, "name");
        lua_pushstring(L, lua_module_type_name(hdr->module_type)); lua_setfield(L, -2, "type");
        lua_pushstring(L, hdr->version);                       lua_setfield(L, -2, "version");
        lua_rawseti(L, -2, out_idx++);
    }
    return 1;
}

static void register_kitt_module(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, lua_radio_send);      lua_setfield(L, -2, "send");
    lua_pushcfunction(L, lua_radio_receive);   lua_setfield(L, -2, "receive");
    lua_pushcfunction(L, lua_radio_available); lua_setfield(L, -2, "available");
    lua_pushcfunction(L, lua_radio_rssi);      lua_setfield(L, -2, "rssi");
    lua_pushcfunction(L, lua_radio_snr);       lua_setfield(L, -2, "snr");
    lua_setglobal(L, "radio");

    lua_newtable(L);
    lua_pushcfunction(L, lua_gps_fix); lua_setfield(L, -2, "fix");
    lua_setglobal(L, "gps");

    lua_newtable(L);
    lua_pushcfunction(L, lua_kitt_modules); lua_setfield(L, -2, "modules");
    lua_setglobal(L, "kitt");
}

// ── win.* ─────────────────────────────────────────────────────────────────────
// Thin Lua wrappers over purr_win.h — the same window/label/button/textarea
// API every native app uses, so a .meow script can build a simple UI.

// Button callbacks: the Lua function is stashed in the registry via
// luaL_ref(), the ref (cast to a pointer-sized int) is the callback's
// `user` — see the threading-model note at the top of this file for why
// this trampoline doesn't need its own locking.
static void lua_btn_trampoline(purr_wid_t wid, purr_event_t event, void *user) {
    (void)wid; (void)event;
    if (!s_L) return;
    int ref = (int)(intptr_t)user;
    lua_rawgeti(s_L, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(s_L, 0, 0, 0) != LUA_OK) {
        ESP_LOGE(TAG, "button callback error: %s", lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
    }
}

static int lua_win_create(lua_State *L) {
    const char *title = luaL_checkstring(L, 1);
    lua_pushinteger(L, (lua_Integer)purr_win_create(title));
    return 1;
}

static int lua_win_destroy(lua_State *L) {
    purr_win_destroy((purr_win_t)luaL_checkinteger(L, 1));
    return 0;
}

static int lua_win_show(lua_State *L) {
    purr_win_show((purr_win_t)luaL_checkinteger(L, 1));
    return 0;
}

static int lua_win_label(lua_State *L) {
    purr_win_t win = (purr_win_t)luaL_checkinteger(L, 1);
    const char *text = luaL_checkstring(L, 2);
    lua_pushinteger(L, (lua_Integer)purr_win_label(win, text));
    return 1;
}

static int lua_win_label_set(lua_State *L) {
    purr_wid_t wid = (purr_wid_t)luaL_checkinteger(L, 1);
    purr_win_label_set(wid, luaL_checkstring(L, 2));
    return 0;
}

static int lua_win_button(lua_State *L) {
    purr_win_t win = (purr_win_t)luaL_checkinteger(L, 1);
    const char *text = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_pushvalue(L, 3);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    purr_wid_t wid = purr_win_button(win, text, lua_btn_trampoline, (void *)(intptr_t)ref);
    lua_pushinteger(L, (lua_Integer)wid);
    return 1;
}

static int lua_win_textarea(lua_State *L) {
    purr_win_t win = (purr_win_t)luaL_checkinteger(L, 1);
    lua_Integer w = luaL_optinteger(L, 2, 100);
    lua_Integer h = luaL_optinteger(L, 3, 60);
    lua_pushinteger(L, (lua_Integer)purr_win_textarea(win, (uint16_t)w, (uint16_t)h));
    return 1;
}

static int lua_win_textarea_set(lua_State *L) {
    purr_win_textarea_set((purr_wid_t)luaL_checkinteger(L, 1), luaL_checkstring(L, 2));
    return 0;
}

static int lua_win_textarea_get(lua_State *L) {
    const char *s = purr_win_textarea_get((purr_wid_t)luaL_checkinteger(L, 1));
    lua_pushstring(L, s ? s : "");
    return 1;
}

static int lua_win_label_align(lua_State *L) {
    purr_wid_t wid = (purr_wid_t)luaL_checkinteger(L, 1);
    // 0=left, 1=center, 2=right — matches purr_align_t (catcall_ui.h).
    purr_win_label_align(wid, (purr_align_t)luaL_checkinteger(L, 2));
    return 0;
}

// ── Layout containers ─────────────────────────────────────────────────────────
// Thin wrappers over purr_win_row/col/row_grow/col_grow/layout_end — needed
// for any script that lays out more than one widget per line (e.g. a
// button grid), since without a container every widget added to a window
// stacks in one single column.

static int lua_win_row(lua_State *L) {
    purr_win_t win = (purr_win_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (lua_Integer)purr_win_row(win, (uint8_t)luaL_optinteger(L, 2, 4)));
    return 1;
}

static int lua_win_col(lua_State *L) {
    purr_win_t win = (purr_win_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (lua_Integer)purr_win_col(win, (uint8_t)luaL_optinteger(L, 2, 4)));
    return 1;
}

static int lua_win_row_grow(lua_State *L) {
    purr_win_t win = (purr_win_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (lua_Integer)purr_win_row_grow(win, (uint8_t)luaL_optinteger(L, 2, 4)));
    return 1;
}

static int lua_win_col_grow(lua_State *L) {
    purr_win_t win = (purr_win_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (lua_Integer)purr_win_col_grow(win, (uint8_t)luaL_optinteger(L, 2, 4)));
    return 1;
}

static int lua_win_layout_end(lua_State *L) {
    purr_win_layout_end((purr_wid_t)luaL_checkinteger(L, 1));
    return 0;
}

static void register_win_module(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, lua_win_create);        lua_setfield(L, -2, "create");
    lua_pushcfunction(L, lua_win_destroy);       lua_setfield(L, -2, "destroy");
    lua_pushcfunction(L, lua_win_show);          lua_setfield(L, -2, "show");
    lua_pushcfunction(L, lua_win_label);         lua_setfield(L, -2, "label");
    lua_pushcfunction(L, lua_win_label_set);     lua_setfield(L, -2, "label_set");
    lua_pushcfunction(L, lua_win_label_align);   lua_setfield(L, -2, "label_align");
    lua_pushcfunction(L, lua_win_button);        lua_setfield(L, -2, "button");
    lua_pushcfunction(L, lua_win_textarea);      lua_setfield(L, -2, "textarea");
    lua_pushcfunction(L, lua_win_textarea_set);  lua_setfield(L, -2, "textarea_set");
    lua_pushcfunction(L, lua_win_textarea_get);  lua_setfield(L, -2, "textarea_get");
    lua_pushcfunction(L, lua_win_row);           lua_setfield(L, -2, "row");
    lua_pushcfunction(L, lua_win_col);           lua_setfield(L, -2, "col");
    lua_pushcfunction(L, lua_win_row_grow);      lua_setfield(L, -2, "row_grow");
    lua_pushcfunction(L, lua_win_col_grow);      lua_setfield(L, -2, "col_grow");
    lua_pushcfunction(L, lua_win_layout_end);    lua_setfield(L, -2, "layout_end");
    lua_setglobal(L, "win");
}

// ── Run ───────────────────────────────────────────────────────────────────────

bool lua_run_file(const char *path) {
    if (!s_L) { ESP_LOGE(TAG, "runtime not initialized"); return false; }

    ESP_LOGI(TAG, "loading %s", path);
    if (luaL_loadfile(s_L, path) != LUA_OK) {
        ESP_LOGE(TAG, "load error: %s", lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return false;
    }
    if (lua_pcall(s_L, 0, 0, 0) != LUA_OK) {
        ESP_LOGE(TAG, "runtime error: %s", lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return false;
    }
    return true;
}

bool lua_run_code(const char *code, const char *name) {
    if (!s_L) { ESP_LOGE(TAG, "runtime not initialized"); return false; }

    if (luaL_loadbuffer(s_L, code, strlen(code), name) != LUA_OK) {
        ESP_LOGE(TAG, "compile error: %s", lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return false;
    }
    if (lua_pcall(s_L, 0, 0, 0) != LUA_OK) {
        ESP_LOGE(TAG, "runtime error: %s", lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return false;
    }
    return true;
}

// ── Module lifecycle ──────────────────────────────────────────────────────────

int lua_runtime_init(void) {
    // This init() runs twice in practice: once at boot, like every other
    // PURR_MOD_SYSTEM module (harmless no-op — no .meow launch is pending
    // yet), and again each time app_manager's launch_meow() actually
    // launches a .meow script. Check for pending work before allocating a
    // Lua state at all, so the boot-time call doesn't leak one.
    const char *code = app_manager_get_pending_meow_code(NULL);
    const char *path = app_manager_get_pending_meow_path();
    if (!code && (!path || !path[0])) {
        return 0;   // nothing pending — not an error, just not our turn yet
    }

    if (s_L) lua_runtime_deinit();   // previous script's VM, if any

    s_L = luaL_newstate();
    if (!s_L) { ESP_LOGE(TAG, "failed to create Lua state"); return -1; }

    luaL_openlibs(s_L);
    register_system_module(s_L);
    register_sd_module(s_L);
    register_win_module(s_L);
    // .hiss only — see app_manager.h's app_tier_t doc and this file's
    // register_kitt_module() comment. A .meow script never gets these
    // globals registered into its Lua state at all.
    if (app_manager_get_pending_meow_privileged()) {
        register_kitt_module(s_L);
    }

    // Preferred path: app_manager already read the script into a PSRAM
    // buffer (see launch_meow()'s comment) so this never has to fopen()
    // anything itself, which is what let meow_task() move back onto a
    // PSRAM-backed stack. lua_run_file() stays as a fallback/general-purpose
    // entry point (e.g. a future terminal/REPL caller) — this file's own
    // luaL_loadfile() path, not something app_manager's launch flow uses
    // anymore.
    bool ok = code ? lua_run_code(code, (path && path[0]) ? path : "meow")
                    : lua_run_file(path);
    return ok ? 0 : -1;
}

void lua_runtime_deinit(void) {
    if (s_L) {
        lua_close(s_L);
        s_L = NULL;
    }
}

// ── .purr module header ───────────────────────────────────────────────────────

PURR_MODULE_REGISTER(lua_runtime) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_SYSTEM,
    .load_priority     = PURR_PRIORITY_IMPORTANT,
    .name              = "lua_runtime",
    .version           = "0.1.0",
    .kernel_min        = "0.11.1",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = lua_runtime_init,
    .deinit            = lua_runtime_deinit,
};
