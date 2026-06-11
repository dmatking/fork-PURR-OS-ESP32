// stub_managers.cpp — linker stubs for modules excluded from the factory kernel.
// Compiled only for PURR_IS_BOOTLOADER_IMG (cyd_boot).
// kitt.cpp calls these unconditionally; the stubs satisfy the linker without
// pulling in the full WiFi stack or power management code.

#ifdef PURR_IS_BOOTLOADER_IMG

#include "wifi_manager.h"
#include "power_manager.h"

// ── WiFi stubs ────────────────────────────────────────────────────────────────
void wifi_manager_init()                                {}
void wifi_manager_update()                              {}
void wifi_manager_deinit()                              {}
bool wifi_manager_enabled()                             { return false; }
void wifi_manager_enable()                              {}
void wifi_manager_disable()                             {}
bool wifi_manager_scan_start()                          { return false; }
bool wifi_manager_scan_done()                           { return false; }
int  wifi_manager_scan_count()                          { return 0; }
void wifi_manager_scan_get_ssid(int, char* b, size_t l) { if (l) b[0] = '\0'; }
int  wifi_manager_scan_get_rssi(int)                    { return 0; }
bool wifi_manager_scan_get_secured(int)                 { return false; }
void wifi_manager_connect(const char*, const char*)     {}
void wifi_manager_disconnect()                          {}
bool wifi_manager_connected()                           { return false; }
void wifi_manager_get_ssid(char* b, size_t l)           { if (l) b[0] = '\0'; }
int  wifi_manager_rssi()                                { return 0; }
void wifi_manager_forget(const char*)                   {}
void wifi_manager_yield()                               {}
void wifi_manager_reclaim()                             {}
bool wifi_manager_yielded()                             { return false; }

// ── Power manager stubs ───────────────────────────────────────────────────────
void power_manager_init(uint16_t)                       {}
void power_manager_update()                             {}
void power_manager_deinit()                             {}
void power_manager_refresh_battery()                    {}
int  power_manager_battery_percent()                    { return 0; }
int  power_manager_battery_voltage_mv()                 { return 0; }
int  power_manager_battery_current_ma()                 { return 0; }
bool power_manager_battery_charging()                   { return false; }
void power_manager_cpu_set_freq(int)                    {}
int  power_manager_cpu_get_freq()                       { return 240; }

#endif  // PURR_IS_BOOTLOADER_IMG
