// Task manager — control background .claw apps

#include "app_task_manager.h"
#include "app_lua_window.h"
#include "miniwin.h"
#include "gl/gl.h"
#include <string.h>
#include <cstdio>

// Extern from app_launcher.cpp
extern app_lua_window_t **app_launcher_get_background_tasks(int *count);
extern void app_launcher_remove_background_task(app_lua_window_t *w);

int app_task_manager_get_tasks(bg_task_t *tasks, int max)
{
    int count = 0;
    int bg_count = 0;
    app_lua_window_t **bg_tasks = app_launcher_get_background_tasks(&bg_count);
    if (!bg_tasks) return 0;

    for (int i = 0; i < bg_count && count < max; i++) {
        app_lua_window_t *w = bg_tasks[i];
        if (!w) continue;

        tasks[count].handle = (void *)w;
        tasks[count].path = app_lua_window_get_path(w);

        const char *name = strrchr(tasks[count].path, '/');
        tasks[count].name = name ? name + 1 : tasks[count].path;

        tasks[count].is_running = app_lua_window_is_running(w);
        count++;
    }
    return count;
}

bool app_task_manager_kill(void *handle)
{
    app_lua_window_t *w = (app_lua_window_t *)handle;
    if (!w) return false;

    app_launcher_remove_background_task(w);
    app_lua_window_set_background(w, false);
    app_lua_window_free(w);
    return true;
}
