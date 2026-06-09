// app_lua_window.cpp — Lua-driven app window
// Each window gets its own lua_State running in a FreeRTOS task.
// .paws scripts get sandboxed win.* + sd.* APIs only.
// .claw scripts additionally get kitt.* (WiFi, SD status, reboot).
//
// win.* API (retained-mode widget list):
//   win.clear()                        — clear all widgets
//   win.label(text, x, y [,color])     — add a text label
//   win.button(text, x, y [,w,h,id])   — add a clickable button
//   win.rect(x, y, w, h [,color])      — add a filled rectangle
//   win.wait_touch([timeout_ms])        — block until touch, returns {x,y} or nil
//   win.sleep(ms)                       — FreeRTOS delay
//   win.width()                         — client area width
//   win.height()                        — client area height
//
// sd.* API (both .paws and .claw):
//   sd.read(path)                       — returns string or nil, errmsg
//   sd.write(path, data)                — returns bool
//   sd.list(dir)                        — returns table of {name,size,is_dir}
//
// kitt.* API (.claw only):
//   kitt.wifi_connected()               — bool
//   kitt.sd_available()                 — bool
//   kitt.reboot()                       — restarts device
//   kitt.free_ram()                     — bytes

#include "app_lua_window.h"

#ifdef PURR_HAS_LUA

#include "miniwin.h"
#include "gl/gl.h"
#include "purr_apps_common.h"
#include "partition_manager.h"
#include "kitt.h"
#include <lua.hpp>
#include <stdlib.h>
#include <string.h>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "lua_win";

#define MAX_WIDGETS  48
#define LUA_WIN_REG  "purr_win_ptr"

// ── Widget types ──────────────────────────────────────────────────────────────

typedef enum { W_LABEL, W_BUTTON, W_RECT } widget_type_t;

typedef struct {
    widget_type_t          type;
    int16_t                x, y, w, h;
    char                   text[48];
    mw_hal_lcd_colour_t    color;
    int                    id;
} widget_t;

// ── app_lua_window struct ─────────────────────────────────────────────────────

struct app_lua_window {
    bool              is_admin;
    volatile bool     running;
    char              error[256];
    char              path[512];
    lua_State        *L;
    TaskHandle_t      task;
    SemaphoreHandle_t mutex;
    widget_t          widgets[MAX_WIDGETS];
    int               widget_count;
    int16_t           client_w;
    int16_t           client_h;
    // Single-slot touch event from MiniWin callback
    volatile bool     touch_pending;
    volatile int16_t  touch_x;
    volatile int16_t  touch_y;
};

// ── Registry helpers ──────────────────────────────────────────────────────────

static app_lua_window_t *get_win(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_WIN_REG);
    app_lua_window_t *w = (app_lua_window_t *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return w;
}

// ── win.* Lua API ─────────────────────────────────────────────────────────────

static int l_win_clear(lua_State *L)
{
    app_lua_window_t *w = get_win(L);
    if (!w) return 0;
    xSemaphoreTake(w->mutex, portMAX_DELAY);
    w->widget_count = 0;
    xSemaphoreGive(w->mutex);
    return 0;
}

static int l_win_label(lua_State *L)
{
    app_lua_window_t *w = get_win(L);
    if (!w) return 0;
    const char *text = luaL_checkstring(L, 1);
    int16_t x = (int16_t)luaL_checkinteger(L, 2);
    int16_t y = (int16_t)luaL_checkinteger(L, 3);
    mw_hal_lcd_colour_t fg = (mw_hal_lcd_colour_t)luaL_optinteger(L, 4, (lua_Integer)WCE_TXT);

    xSemaphoreTake(w->mutex, portMAX_DELAY);
    if (w->widget_count < MAX_WIDGETS) {
        widget_t *wg = &w->widgets[w->widget_count++];
        wg->type  = W_LABEL;
        wg->x = x; wg->y = y;
        wg->color = fg;
        strncpy(wg->text, text, sizeof(wg->text) - 1);
        wg->text[sizeof(wg->text) - 1] = '\0';
    }
    xSemaphoreGive(w->mutex);
    return 0;
}

static int l_win_button(lua_State *L)
{
    app_lua_window_t *w = get_win(L);
    if (!w) return 0;
    const char *text = luaL_checkstring(L, 1);
    int16_t x  = (int16_t)luaL_checkinteger(L, 2);
    int16_t y  = (int16_t)luaL_checkinteger(L, 3);
    int16_t bw = (int16_t)luaL_optinteger(L, 4, 64);
    int16_t bh = (int16_t)luaL_optinteger(L, 5, 18);
    int     id = (int)luaL_optinteger(L, 6, 0);

    xSemaphoreTake(w->mutex, portMAX_DELAY);
    if (w->widget_count < MAX_WIDGETS) {
        widget_t *wg = &w->widgets[w->widget_count++];
        wg->type = W_BUTTON;
        wg->x = x; wg->y = y; wg->w = bw; wg->h = bh;
        wg->id = id;
        strncpy(wg->text, text, sizeof(wg->text) - 1);
        wg->text[sizeof(wg->text) - 1] = '\0';
    }
    xSemaphoreGive(w->mutex);
    return 0;
}

static int l_win_rect(lua_State *L)
{
    app_lua_window_t *w = get_win(L);
    if (!w) return 0;
    int16_t x  = (int16_t)luaL_checkinteger(L, 1);
    int16_t y  = (int16_t)luaL_checkinteger(L, 2);
    int16_t rw = (int16_t)luaL_checkinteger(L, 3);
    int16_t rh = (int16_t)luaL_checkinteger(L, 4);
    mw_hal_lcd_colour_t color = (mw_hal_lcd_colour_t)luaL_optinteger(L, 5, (lua_Integer)WCE_BAR);

    xSemaphoreTake(w->mutex, portMAX_DELAY);
    if (w->widget_count < MAX_WIDGETS) {
        widget_t *wg = &w->widgets[w->widget_count++];
        wg->type  = W_RECT;
        wg->x = x; wg->y = y; wg->w = rw; wg->h = rh;
        wg->color = color;
    }
    xSemaphoreGive(w->mutex);
    return 0;
}

static int l_win_wait_touch(lua_State *L)
{
    app_lua_window_t *w = get_win(L);
    if (!w) { lua_pushnil(L); return 1; }

    int timeout_ms = (int)luaL_optinteger(L, 1, 30000);
    int elapsed    = 0;

    while (elapsed < timeout_ms) {
        if (w->touch_pending) {
            w->touch_pending = false;
            lua_newtable(L);
            lua_pushinteger(L, w->touch_x); lua_setfield(L, -2, "x");
            lua_pushinteger(L, w->touch_y); lua_setfield(L, -2, "y");
            return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        elapsed += 50;
    }
    lua_pushnil(L);
    return 1;
}

static int l_win_sleep(lua_State *L)
{
    int ms = (int)luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms < 1 ? 1 : ms));
    return 0;
}

static int l_win_width(lua_State *L)
{
    app_lua_window_t *w = get_win(L);
    lua_pushinteger(L, w ? w->client_w : 240);
    return 1;
}

static int l_win_height(lua_State *L)
{
    app_lua_window_t *w = get_win(L);
    lua_pushinteger(L, w ? w->client_h : 180);
    return 1;
}

// ── sd.* Lua API ──────────────────────────────────────────────────────────────

static int l_sd_read(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    FILE *f = fopen(path, "rb");
    if (!f) {
        lua_pushnil(L);
        lua_pushstring(L, "file not found");
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        lua_pushnil(L);
        lua_pushstring(L, "out of memory");
        return 2;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    lua_pushlstring(L, buf, n);
    free(buf);
    return 1;
}

static int l_sd_write(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    FILE *f = fopen(path, "wb");
    if (!f) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "cannot open file");
        return 2;
    }
    size_t nw = fwrite(data, 1, len, f);
    fclose(f);
    lua_pushboolean(L, nw == len);
    return 1;
}

static int l_sd_list(lua_State *L)
{
    const char *dir_path = luaL_checkstring(L, 1);
    lua_newtable(L);
    DIR *d = opendir(dir_path);
    if (!d) return 1;
    int idx = 1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, e->d_name);
        struct stat st; stat(fullpath, &st);
        lua_newtable(L);
        lua_pushstring(L, e->d_name);       lua_setfield(L, -2, "name");
        lua_pushinteger(L, (lua_Integer)st.st_size); lua_setfield(L, -2, "size");
        lua_pushboolean(L, S_ISDIR(st.st_mode));    lua_setfield(L, -2, "is_dir");
        lua_rawseti(L, -2, idx++);
    }
    closedir(d);
    return 1;
}

// ── kitt.* Lua API (.claw only) ───────────────────────────────────────────────

extern KITT kitt;

static int l_kitt_wifi_connected(lua_State *L)
{
    lua_pushboolean(L, kitt.wifi_connected());
    return 1;
}

static int l_kitt_sd_available(lua_State *L)
{
    lua_pushboolean(L, pm_sd_available());
    return 1;
}

static int l_kitt_reboot(lua_State *L)
{
    (void)L;
    esp_restart();
    return 0;
}

static int l_kitt_free_ram(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    return 1;
}

static int l_kitt_time_ms(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)(esp_timer_get_time() / 1000LL));
    return 1;
}

// ── Module registration ───────────────────────────────────────────────────────

static void register_win_api(lua_State *L, app_lua_window_t *w)
{
    lua_pushlightuserdata(L, w);
    lua_setfield(L, LUA_REGISTRYINDEX, LUA_WIN_REG);

    lua_newtable(L);
    lua_pushcfunction(L, l_win_clear);      lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, l_win_label);      lua_setfield(L, -2, "label");
    lua_pushcfunction(L, l_win_button);     lua_setfield(L, -2, "button");
    lua_pushcfunction(L, l_win_rect);       lua_setfield(L, -2, "rect");
    lua_pushcfunction(L, l_win_wait_touch); lua_setfield(L, -2, "wait_touch");
    lua_pushcfunction(L, l_win_sleep);      lua_setfield(L, -2, "sleep");
    lua_pushcfunction(L, l_win_width);      lua_setfield(L, -2, "width");
    lua_pushcfunction(L, l_win_height);     lua_setfield(L, -2, "height");
    lua_setglobal(L, "win");
}

static void register_sd_api(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_sd_read);  lua_setfield(L, -2, "read");
    lua_pushcfunction(L, l_sd_write); lua_setfield(L, -2, "write");
    lua_pushcfunction(L, l_sd_list);  lua_setfield(L, -2, "list");
    lua_setglobal(L, "sd");
}

static void register_kitt_api(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_kitt_wifi_connected); lua_setfield(L, -2, "wifi_connected");
    lua_pushcfunction(L, l_kitt_sd_available);   lua_setfield(L, -2, "sd_available");
    lua_pushcfunction(L, l_kitt_reboot);         lua_setfield(L, -2, "reboot");
    lua_pushcfunction(L, l_kitt_free_ram);       lua_setfield(L, -2, "free_ram");
    lua_pushcfunction(L, l_kitt_time_ms);        lua_setfield(L, -2, "time_ms");
    lua_setglobal(L, "kitt");
}

// ── Lua task ──────────────────────────────────────────────────────────────────

static void lua_task_fn(void *arg)
{
    app_lua_window_t *w = (app_lua_window_t *)arg;

    int status = luaL_loadfile(w->L, w->path);
    if (status != LUA_OK) {
        snprintf(w->error, sizeof(w->error), "load: %s", lua_tostring(w->L, -1));
        lua_pop(w->L, 1);
        ESP_LOGW(TAG, "%s", w->error);
        w->running = false;
        vTaskDelete(NULL);
        return;
    }

    status = lua_pcall(w->L, 0, 0, 0);
    if (status != LUA_OK) {
        const char *msg = lua_tostring(w->L, -1);
        snprintf(w->error, sizeof(w->error), "%s", msg ? msg : "(unknown error)");
        lua_pop(w->L, 1);
        ESP_LOGW(TAG, "runtime: %s", w->error);
    }

    w->running = false;
    vTaskDelete(NULL);
}

// ── Public API ────────────────────────────────────────────────────────────────

app_lua_window_t *app_lua_window_create(const char *script_path, bool is_admin)
{
    app_lua_window_t *w = (app_lua_window_t *)calloc(1, sizeof(app_lua_window_t));
    if (!w) return NULL;

    w->is_admin  = is_admin;
    w->running   = false;
    w->client_w  = 240;
    w->client_h  = 180;
    snprintf(w->path, sizeof(w->path), "%s", script_path ? script_path : "");

    w->mutex = xSemaphoreCreateMutex();
    if (!w->mutex) { free(w); return NULL; }

    w->L = luaL_newstate();
    if (!w->L) { vSemaphoreDelete(w->mutex); free(w); return NULL; }

    luaL_openlibs(w->L);
    register_win_api(w->L, w);
    register_sd_api(w->L);
    if (is_admin)
        register_kitt_api(w->L);

    w->running = true;
    BaseType_t rc = xTaskCreate(lua_task_fn, "lua_app", 8192, w, 3, &w->task);
    if (rc != pdPASS) {
        lua_close(w->L);
        vSemaphoreDelete(w->mutex);
        free(w);
        return NULL;
    }

    ESP_LOGI(TAG, "created: %s  admin=%d", script_path, is_admin);
    return w;
}

void app_lua_window_free(app_lua_window_t *w)
{
    if (!w) return;
    if (w->task && w->running) {
        vTaskDelete(w->task);
        w->task = NULL;
    }
    if (w->L) { lua_close(w->L); w->L = NULL; }
    if (w->mutex) { vSemaphoreDelete(w->mutex); w->mutex = NULL; }
    free(w);
}

void app_lua_window_paint(app_lua_window_t *w,
                          int16_t ww, int16_t wh,
                          const void *draw_info)
{
    if (!w || !draw_info) return;
    const mw_gl_draw_info_t *d = (const mw_gl_draw_info_t *)draw_info;

    // Update cached dims for Lua API
    w->client_w = ww;
    w->client_h = wh;

    // Background
    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, ww, wh);

    // Error state
    if (!w->running && w->error[0]) {
        mw_gl_set_font(MW_GL_FONT_9);
        mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
        mw_gl_set_fg_colour(0xF800u);
        mw_gl_string(d, 4, 4, "Script error:");
        mw_gl_set_fg_colour(WCE_TXT);
        // Word-wrap error at 26 chars
        char line[27]; int pos = 0; int16_t ey = 16;
        const char *p = w->error;
        while (*p && ey < wh - 10) {
            int n = 0;
            while (p[n] && n < 26) { line[n] = p[n]; n++; }
            line[n] = '\0';
            mw_gl_string(d, 4, ey, line);
            p += n; ey += 12;
        }
        return;
    }

    // Render widget list under mutex
    xSemaphoreTake(w->mutex, portMAX_DELAY);

    mw_gl_set_font(MW_GL_FONT_9);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);

    for (int i = 0; i < w->widget_count; i++) {
        widget_t *wg = &w->widgets[i];
        switch (wg->type) {
        case W_LABEL:
            mw_gl_set_fg_colour(wg->color ? wg->color : (mw_hal_lcd_colour_t)WCE_TXT);
            mw_gl_string(d, wg->x, wg->y, wg->text);
            break;

        case W_BUTTON: {
            // Raised 3D button
            mw_gl_set_fill(MW_GL_FILL);
            mw_gl_set_border(MW_GL_BORDER_OFF);
            mw_gl_set_solid_fill_colour(WCE_BAR);
            mw_gl_rectangle(d, wg->x, wg->y, wg->w, wg->h);
            mw_gl_set_fg_colour(WCE_HI);
            mw_gl_hline(d, wg->x, (int16_t)(wg->x + wg->w - 1), wg->y);
            mw_gl_vline(d, wg->x, wg->y, (int16_t)(wg->y + wg->h - 1));
            mw_gl_set_fg_colour(WCE_SHD);
            mw_gl_hline(d, (int16_t)(wg->x + 1), (int16_t)(wg->x + wg->w - 2),
                        (int16_t)(wg->y + wg->h - 2));
            mw_gl_vline(d, (int16_t)(wg->x + wg->w - 2),
                        (int16_t)(wg->y + 1), (int16_t)(wg->y + wg->h - 2));
            mw_gl_set_fg_colour(WCE_DARK);
            mw_gl_hline(d, wg->x, (int16_t)(wg->x + wg->w - 1),
                        (int16_t)(wg->y + wg->h - 1));
            mw_gl_vline(d, (int16_t)(wg->x + wg->w - 1),
                        wg->y, (int16_t)(wg->y + wg->h - 1));
            mw_gl_set_fg_colour(WCE_TXT);
            mw_gl_string(d, (int16_t)(wg->x + 4),
                         (int16_t)(wg->y + (wg->h - 9) / 2), wg->text);
            break;
        }

        case W_RECT:
            mw_gl_set_fill(MW_GL_FILL);
            mw_gl_set_border(MW_GL_BORDER_OFF);
            mw_gl_set_solid_fill_colour(wg->color);
            mw_gl_rectangle(d, wg->x, wg->y, wg->w, wg->h);
            break;
        }
    }

    xSemaphoreGive(w->mutex);
}

void app_lua_window_on_message(app_lua_window_t *w,
                               uint32_t msg_id, uint32_t msg_data)
{
    if (!w) return;
    if (msg_id == MW_TOUCH_DOWN_MESSAGE) {
        // MiniWin delivers client-relative coords: high16=x, low16=y
        w->touch_x       = (int16_t)(msg_data >> 16);
        w->touch_y       = (int16_t)(msg_data & 0xFFFF);
        w->touch_pending = true;
    }
}

bool app_lua_window_is_running(app_lua_window_t *w)
{
    return w && w->running;
}

const char *app_lua_window_get_error(app_lua_window_t *w)
{
    return w ? w->error : "null";
}

#else  // PURR_HAS_LUA not defined — stub implementation

#include <stdlib.h>
#include <string.h>
#include <cstdio>

struct app_lua_window {
    bool is_admin;
    bool running;
    char error[64];
};

app_lua_window_t *app_lua_window_create(const char *script_path, bool is_admin)
{
    (void)script_path;
    app_lua_window_t *w = (app_lua_window_t *)calloc(1, sizeof(app_lua_window_t));
    if (!w) return NULL;
    w->is_admin = is_admin;
    w->running  = false;
    snprintf(w->error, sizeof(w->error), "Lua not enabled (PURR_ENABLE_LUA=0)");
    return w;
}

void app_lua_window_free(app_lua_window_t *w) { free(w); }

void app_lua_window_paint(app_lua_window_t *w,
                          int16_t w_width, int16_t w_height,
                          const void *draw_info)
{
    (void)w; (void)w_width; (void)w_height; (void)draw_info;
}

void app_lua_window_on_message(app_lua_window_t *w,
                               uint32_t msg_id, uint32_t msg_data)
{
    (void)w; (void)msg_id; (void)msg_data;
}

bool app_lua_window_is_running(app_lua_window_t *w)
{
    (void)w; return false;
}

const char *app_lua_window_get_error(app_lua_window_t *w)
{
    return w ? w->error : "null";
}

#endif  // PURR_HAS_LUA
