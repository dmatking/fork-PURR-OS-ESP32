#pragma once
#include <stdbool.h>
#include "purr_sys_drv.h"

void mtp_manager_init();
void mtp_manager_tick();
void mtp_manager_deinit();
void mtp_manager_drv_register(bool enabled);
bool mtp_manager_active();
void mtp_manager_enter();
void mtp_manager_exit();
