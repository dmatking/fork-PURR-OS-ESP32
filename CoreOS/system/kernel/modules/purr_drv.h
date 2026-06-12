#pragma once
#include <stdbool.h>

// PURR Driver Manager
// Loads .drv scripts from /sdcard/drvdebug/, runs init()/tick()/deinit().
// Shell command: drvmgr

#define PURR_DRV_MAX 8

void purr_drv_init(void);
void purr_drv_tick(void);           // call every ~100ms from main loop

bool purr_drv_load(const char *path, char *err, int err_len);
bool purr_drv_unload(const char *name);
bool purr_drv_cmd(const char *name, const char *args, char *out, int out_len);
int  purr_drv_list(char out[][32], int max);  // returns count
