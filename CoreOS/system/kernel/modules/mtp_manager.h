#pragma once
#include <stdbool.h>

void mtp_manager_init();
void mtp_manager_update();
void mtp_manager_deinit();
bool mtp_manager_active();
void mtp_manager_enter();
void mtp_manager_exit();
