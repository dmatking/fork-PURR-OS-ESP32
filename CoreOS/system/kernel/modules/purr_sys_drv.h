#pragma once
#include <stdint.h>
#include <stdbool.h>

// Unified system driver registry.
//
// Every compiled-in hardware driver registers a sys_drv_t descriptor.
// KITT calls sys_drv_init_all() / sys_drv_tick_all() / sys_drv_deinit_all()
// instead of hard-coding each manager call.
//
// Compile-time enable/disable: set drv->enabled before sys_drv_init_all().
// Runtime listing: drvmgr list shows all registered sys drivers alongside PDL drivers.

#define SYS_DRV_MAX 24

typedef struct sys_drv_t {
    const char *name;       // e.g. "wifi", "bt", "lora", "display:st7789", "touch:gt911"
    const char *subsystem;  // "radio" | "display" | "touch" | "power" | "io" | "net"
    bool        enabled;    // false = skip init/tick — set by KITT from device config

    void (*init)   (void);                                       // NULL = no-op
    void (*tick)   (void);                                       // NULL = no-op (~10ms)
    void (*deinit) (void);                                       // NULL = no-op
    int  (*cmd)    (const char *args, char *out, int out_len);   // NULL = unsupported
} sys_drv_t;

#ifdef __cplusplus
extern "C" {
#endif

void             sys_drv_register (sys_drv_t *drv);
void             sys_drv_init_all (void);
void             sys_drv_tick_all (void);
void             sys_drv_deinit_all (void);
int              sys_drv_cmd      (const char *name, const char *args, char *out, int out_len);
int              sys_drv_list     (sys_drv_t **out, int max);  // returns count
const sys_drv_t *sys_drv_find     (const char *name);

#ifdef __cplusplus
}
#endif
