#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *handle;
    const char *path;
    const char *name;
    bool is_running;
} bg_task_t;

// Get list of background tasks
int app_task_manager_get_tasks(bg_task_t *tasks, int max);

// Kill a background task by handle
bool app_task_manager_kill(void *handle);

#ifdef __cplusplus
}
#endif
