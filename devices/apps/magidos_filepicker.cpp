#include "magidos_filepicker.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <string.h>
#include <stdio.h>

static const char *TAG     = "magidos_fp";
static const char *SD_PATH = "/sdcard";

#define MAX_FILES   32
#define NAME_LEN    64

static char s_files[MAX_FILES][NAME_LEN];
static char s_selected[NAME_LEN + 16];
static int  s_count = 0;
static int  s_selected_idx = 0;

static bool _is_dos_exec(const char *name)
{
    size_t l = strlen(name);
    if (l < 5) return false;
    const char *ext = name + l - 4;
    return (strcasecmp(ext, ".com") == 0 || strcasecmp(ext, ".exe") == 0);
}

static void _scan(void)
{
    s_count = 0;
    DIR *d = opendir(SD_PATH);
    if (!d) {
        ESP_LOGW(TAG, "SD card not mounted at %s", SD_PATH);
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && s_count < MAX_FILES) {
        if (_is_dos_exec(ent->d_name)) {
            strncpy(s_files[s_count], ent->d_name, NAME_LEN - 1);
            s_files[s_count][NAME_LEN - 1] = '\0';
            s_count++;
        }
    }
    closedir(d);
    ESP_LOGI(TAG, "found %d DOS executables on SD", s_count);
}

const char *magidos_filepicker_show(void)
{
    _scan();
    if (s_count == 0) {
        ESP_LOGW(TAG, "no .COM or .EXE files found on SD card");
        return NULL;
    }
    s_selected_idx = 0;
    snprintf(s_selected, sizeof(s_selected), "%.60s/%.59s", SD_PATH, s_files[s_selected_idx]);
    ESP_LOGI(TAG, "selected: %s", s_selected);
    return s_selected;
}

void magidos_filepicker_nav(int dir)
{
    s_selected_idx += dir;
    if (s_selected_idx < 0) s_selected_idx = 0;
    if (s_selected_idx >= s_count) s_selected_idx = s_count - 1;
}

const char *magidos_filepicker_get_selected(void)
{
    if (s_count == 0) return NULL;
    snprintf(s_selected, sizeof(s_selected), "%.60s/%.59s", SD_PATH, s_files[s_selected_idx]);
    return s_selected;
}

int magidos_filepicker_count(void)
{
    return s_count;
}
