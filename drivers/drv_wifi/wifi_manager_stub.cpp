// No-op WiFi stubs for targets that don't use the WiFi stack (e.g. heltec).
#include "wifi_manager.h"
#include <string.h>

void wifi_manager_init()    {}
void wifi_manager_tick()  {}
void wifi_manager_deinit()  {}

bool wifi_manager_enabled()  { return false; }
void wifi_manager_enable()   {}
void wifi_manager_disable()  {}

void wifi_manager_scan_start()              {}
bool wifi_manager_scan_done()               { return false; }
int  wifi_manager_scan_count()              { return 0; }
void wifi_manager_scan_get_ssid(int, char* buf, size_t len) { if (buf && len) buf[0] = '\0'; }
int  wifi_manager_scan_get_rssi(int)        { return 0; }
bool wifi_manager_scan_get_secured(int)     { return false; }

void wifi_manager_connect(const char*, const char*) {}
void wifi_manager_disconnect()              {}
bool wifi_manager_connected()               { return false; }
void wifi_manager_get_ssid(char* buf, size_t len) { if (buf && len) buf[0] = '\0'; }
int  wifi_manager_rssi()                    { return 0; }
void wifi_manager_forget(const char*)       {}
void wifi_manager_yield()                   {}
void wifi_manager_reclaim()                 {}
bool wifi_manager_yielded()                 { return false; }
void wifi_manager_drv_register(bool e) { (void)e; }
