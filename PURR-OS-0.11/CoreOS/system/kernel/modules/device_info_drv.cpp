// device_info_drv.cpp — sys_drv exposing device_config fields via cmd()
// drvmgr cmd device_info <key>   e.g. key = name|display|res|ram|psram|flash|wifi|bt|lora|cpu|all

#include "device_info_drv.h"
#include "purr_sys_drv.h"
#include "device_config.h"
#include <string.h>
#include <stdio.h>

static device_config_t s_cfg;
static bool            s_loaded = false;

static void dev_init(void) {
    if (!device_config_default(&s_cfg)) {
        device_config_load("/sdcard/device.json", &s_cfg);
    }
    s_loaded = true;
}

static int dev_cmd(const char *args, char *out, int out_len) {
    if (!s_loaded) { snprintf(out, out_len, "not ready"); return 0; }
    if (!args || !*args || strcmp(args, "all") == 0) {
        snprintf(out, out_len,
            "device=%s display=%s res=%dx%d touch=%s "
            "ram=%ukB psram=%uMB flash=%uMB "
            "wifi=%d bt=%d lora=%d cpu=%uMHz",
            s_cfg.device, s_cfg.display, s_cfg.display_w, s_cfg.display_h,
            s_cfg.touch, s_cfg.ram_kb, s_cfg.psram_mb, s_cfg.flash_mb,
            (int)s_cfg.wifi, (int)s_cfg.bt, (int)s_cfg.lora, s_cfg.cpu_max_mhz);
        return 1;
    }
    if (strcmp(args, "name")    == 0) { snprintf(out, out_len, "%s", s_cfg.device);  return 1; }
    if (strcmp(args, "display") == 0) { snprintf(out, out_len, "%s", s_cfg.display); return 1; }
    if (strcmp(args, "res")     == 0) { snprintf(out, out_len, "%dx%d", s_cfg.display_w, s_cfg.display_h); return 1; }
    if (strcmp(args, "touch")   == 0) { snprintf(out, out_len, "%s", s_cfg.touch);   return 1; }
    if (strcmp(args, "ram")     == 0) { snprintf(out, out_len, "%ukB", s_cfg.ram_kb); return 1; }
    if (strcmp(args, "psram")   == 0) { snprintf(out, out_len, "%uMB", s_cfg.psram_mb); return 1; }
    if (strcmp(args, "flash")   == 0) { snprintf(out, out_len, "%uMB", s_cfg.flash_mb); return 1; }
    if (strcmp(args, "wifi")    == 0) { snprintf(out, out_len, "%d", (int)s_cfg.wifi); return 1; }
    if (strcmp(args, "bt")      == 0) { snprintf(out, out_len, "%d", (int)s_cfg.bt);   return 1; }
    if (strcmp(args, "lora")    == 0) { snprintf(out, out_len, "%d", (int)s_cfg.lora); return 1; }
    if (strcmp(args, "cpu")     == 0) { snprintf(out, out_len, "%uMHz", s_cfg.cpu_max_mhz); return 1; }
    snprintf(out, out_len, "unknown key '%s'", args);
    return 0;
}

static sys_drv_t s_dev_drv = {
    .name      = "device_info",
    .subsystem = "io",
    .enabled   = true,
    .init      = dev_init,
    .tick      = NULL,
    .deinit    = NULL,
    .cmd       = dev_cmd,
};

void device_info_drv_register(void) {
    sys_drv_register(&s_dev_drv);
}
