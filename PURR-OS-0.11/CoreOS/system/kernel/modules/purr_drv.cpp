// purr_drv.cpp — PURR Driver Manager
// Manages up to PURR_DRV_MAX loaded .drv scripts.

#include "purr_drv.h"
#include "purr_drv_interp.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "drvmgr";

static pdrv_script_t *s_slots[PURR_DRV_MAX] = {};

void purr_drv_init(void) {
    memset(s_slots, 0, sizeof(s_slots));
    ESP_LOGI(TAG, "driver manager ready (max %d slots)", PURR_DRV_MAX);
}

void purr_drv_tick(void) {
    char err[128];
    for (int i = 0; i < PURR_DRV_MAX; i++) {
        if (!s_slots[i]) continue;
        if (!pdrv_call(s_slots[i], "tick", NULL, err, sizeof(err)))
            ESP_LOGW(TAG, "[%s] tick error: %s", pdrv_name(s_slots[i]), err);
    }
}

bool purr_drv_load(const char *path, char *err, int err_len) {
    // find free slot
    int slot = -1;
    for (int i = 0; i < PURR_DRV_MAX; i++) if (!s_slots[i]) { slot = i; break; }
    if (slot < 0) { snprintf(err, err_len, "no free driver slots"); return false; }

    // read file
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, err_len, "cannot open '%s'", path); return false; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz > 32768) { fclose(f); snprintf(err, err_len, "file too large (%ld bytes)", sz); return false; }
    char *src = (char*)malloc(sz + 1);
    if (!src) { fclose(f); snprintf(err, err_len, "out of memory"); return false; }
    fread(src, 1, sz, f); src[sz] = '\0'; fclose(f);

    // parse
    pdrv_script_t *s = pdrv_parse(src, err, err_len);
    free(src);
    if (!s) return false;

    // check not already loaded
    for (int i = 0; i < PURR_DRV_MAX; i++) {
        if (s_slots[i] && !strcmp(pdrv_name(s_slots[i]), pdrv_name(s))) {
            pdrv_free(s);
            snprintf(err, err_len, "driver '%s' already loaded", pdrv_name(s));
            return false;
        }
    }

    s_slots[slot] = s;
    ESP_LOGI(TAG, "loaded driver '%s' in slot %d", pdrv_name(s), slot);

    // call init()
    char init_err[128] = {};
    if (!pdrv_call(s, "init", NULL, init_err, sizeof(init_err)))
        ESP_LOGW(TAG, "init() error: %s", init_err);

    return true;
}

bool purr_drv_unload(const char *name) {
    for (int i = 0; i < PURR_DRV_MAX; i++) {
        if (!s_slots[i]) continue;
        if (strcmp(pdrv_name(s_slots[i]), name) != 0) continue;

        char err[128] = {};
        pdrv_call(s_slots[i], "deinit", NULL, err, sizeof(err));
        pdrv_free(s_slots[i]);
        s_slots[i] = NULL;
        ESP_LOGI(TAG, "unloaded driver '%s'", name);
        return true;
    }
    return false;
}

bool purr_drv_cmd(const char *name, const char *args, char *out, int out_len) {
    for (int i = 0; i < PURR_DRV_MAX; i++) {
        if (!s_slots[i]) continue;
        if (strcmp(pdrv_name(s_slots[i]), name) != 0) continue;
        char err[128] = {};
        bool ok = pdrv_call(s_slots[i], "cmd", args, err, sizeof(err));
        if (!ok) snprintf(out, out_len, "error: %s", err);
        else snprintf(out, out_len, "ok");
        return ok;
    }
    snprintf(out, out_len, "driver '%s' not found", name);
    return false;
}

int purr_drv_list(char out[][32], int max) {
    int n = 0;
    for (int i = 0; i < PURR_DRV_MAX && n < max; i++) {
        if (s_slots[i]) strncpy(out[n++], pdrv_name(s_slots[i]), 31);
    }
    return n;
}
