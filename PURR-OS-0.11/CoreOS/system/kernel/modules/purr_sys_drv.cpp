// purr_sys_drv.cpp — Unified system driver registry

#include "purr_sys_drv.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "sysdrv";

static sys_drv_t *s_reg[SYS_DRV_MAX];
static int        s_n = 0;

void sys_drv_register(sys_drv_t *drv) {
    if (!drv || s_n >= SYS_DRV_MAX) {
        ESP_LOGW(TAG, "register failed: %s", drv ? drv->name : "null");
        return;
    }
    s_reg[s_n++] = drv;
    ESP_LOGD(TAG, "registered [%s] enabled=%d", drv->name, (int)drv->enabled);
}

void sys_drv_init_all(void) {
    for (int i = 0; i < s_n; i++) {
        sys_drv_t *d = s_reg[i];
        if (!d->enabled || !d->init) continue;
        ESP_LOGI(TAG, "init [%s]", d->name);
        d->init();
    }
}

void sys_drv_tick_all(void) {
    for (int i = 0; i < s_n; i++) {
        sys_drv_t *d = s_reg[i];
        if (!d->enabled || !d->tick) continue;
        d->tick();
    }
}

void sys_drv_deinit_all(void) {
    for (int i = s_n - 1; i >= 0; i--) {
        sys_drv_t *d = s_reg[i];
        if (!d->enabled || !d->deinit) continue;
        ESP_LOGI(TAG, "deinit [%s]", d->name);
        d->deinit();
    }
}

int sys_drv_cmd(const char *name, const char *args, char *out, int out_len) {
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_reg[i]->name, name) == 0) {
            if (!s_reg[i]->cmd) {
                snprintf(out, out_len, "driver '%s' has no cmd handler", name);
                return -1;
            }
            return s_reg[i]->cmd(args, out, out_len);
        }
    }
    snprintf(out, out_len, "driver '%s' not found", name);
    return -1;
}

int sys_drv_list(sys_drv_t **out, int max) {
    int n = (s_n < max) ? s_n : max;
    if (out) {
        for (int i = 0; i < n; i++) out[i] = s_reg[i];
    }
    return s_n;
}

const sys_drv_t *sys_drv_find(const char *name) {
    for (int i = 0; i < s_n; i++)
        if (strcmp(s_reg[i]->name, name) == 0) return s_reg[i];
    return NULL;
}
