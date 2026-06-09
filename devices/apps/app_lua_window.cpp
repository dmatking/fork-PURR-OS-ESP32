#include "app_lua_window.h"
#include <stdlib.h>
#include <string.h>
#include <cstdio>

struct app_lua_window {
    bool is_admin;
    bool running;
    char error[256];
};

app_lua_window_t* app_lua_window_create(const char* script_path, bool is_admin)
{
    app_lua_window_t *w = (app_lua_window_t *)malloc(sizeof(app_lua_window_t));
    if (!w) return NULL;

    memset(w, 0, sizeof(*w));
    w->is_admin = is_admin;
    w->running = false;
    snprintf(w->error, sizeof(w->error), "Lua support not yet enabled");
    return w;
}

void app_lua_window_free(app_lua_window_t* w)
{
    if (!w) return;
    free(w);
}

void app_lua_window_paint(app_lua_window_t* w, int16_t w_width, int16_t w_height, const void *draw_info)
{
    (void)w; (void)w_width; (void)w_height; (void)draw_info;
}

void app_lua_window_on_message(app_lua_window_t* w, uint32_t msg_id, uint32_t msg_data)
{
    (void)w; (void)msg_id; (void)msg_data;
}

bool app_lua_window_is_running(app_lua_window_t* w)
{
    return false;
}

const char* app_lua_window_get_error(app_lua_window_t* w)
{
    return w ? w->error : "No error";
}
