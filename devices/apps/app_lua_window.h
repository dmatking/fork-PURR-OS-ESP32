#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_lua_window app_lua_window_t;

app_lua_window_t* app_lua_window_create(const char* script_path, bool is_admin);
void              app_lua_window_free(app_lua_window_t* w);

void              app_lua_window_paint(app_lua_window_t* w, int16_t w_width, int16_t w_height, const void *draw_info);
void              app_lua_window_on_message(app_lua_window_t* w, uint32_t msg_id, uint32_t msg_data);

bool              app_lua_window_is_running(app_lua_window_t* w);
const char*       app_lua_window_get_error(app_lua_window_t* w);
bool              app_lua_window_get_background(app_lua_window_t* w);
void              app_lua_window_set_background(app_lua_window_t* w, bool bg);
const char*       app_lua_window_get_path(app_lua_window_t* w);

#ifdef __cplusplus
}
#endif
