#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "purr_sys_drv.h"

void wifi_manager_init();
void wifi_manager_tick();
void wifi_manager_deinit();
void wifi_manager_drv_register(bool enabled);  // register into sys_drv registry

bool wifi_manager_enabled();
void wifi_manager_enable();
void wifi_manager_disable();
bool wifi_manager_scan_start();
bool wifi_manager_scan_done();
int  wifi_manager_scan_count();
void wifi_manager_scan_get_ssid(int index, char* buf, size_t len);
int  wifi_manager_scan_get_rssi(int index);
bool wifi_manager_scan_get_secured(int index);
void wifi_manager_connect(const char* ssid, const char* password);
void wifi_manager_disconnect();
bool wifi_manager_connected();
void wifi_manager_get_ssid(char* buf, size_t len);
int  wifi_manager_rssi();
void wifi_manager_forget(const char* ssid);
void wifi_manager_yield();
void wifi_manager_reclaim();
bool wifi_manager_yielded();
