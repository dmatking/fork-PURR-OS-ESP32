#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BT_MAX_PAIRED    8
#define BT_MAX_DISCOVERED 16

void bt_manager_init();
void bt_manager_update();
void bt_manager_deinit();

bool bt_manager_enabled();
void bt_manager_enable();
void bt_manager_disable();
int  bt_manager_paired_count();
void bt_manager_get_paired_name(int index, char* buf, size_t len);
void bt_manager_get_paired_addr(int index, char* buf, size_t len);
bool bt_manager_device_connected(int index);
void bt_manager_start_discovery(uint32_t timeout_ms);
void bt_manager_stop_discovery();
bool bt_manager_discovery_active();
int  bt_manager_discovered_count();
void bt_manager_get_discovered_name(int index, char* buf, size_t len);
void bt_manager_get_discovered_addr(int index, char* buf, size_t len);
void bt_manager_pair(int discovered_index);
void bt_manager_unpair(int paired_index);
void bt_manager_yield();
void bt_manager_reclaim();
bool bt_manager_yielded();
