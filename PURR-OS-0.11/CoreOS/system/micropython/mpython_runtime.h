#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// MicroPython runtime — embed a single interpreter instance.
// Requires: MicroPython added as an ESP-IDF component (idf_component_manager
// or manual clone into components/micropython).

#ifdef __cplusplus
extern "C" {
#endif

// ── Runtime lifecycle ─────────────────────────────────────────────────────────

void mpython_init(void);       // call once at system startup
void mpython_deinit(void);

// Launch a .meow bundle. Reads <meow_path>/main.py from SPIFFS and runs it
// in a new FreeRTOS task. Returns false if already running or file missing.
bool mpython_exec_app(const char* meow_path);

// Process queries (mirrors KITT::process_* API)
bool     mpython_process_running(const char* meow_path);
void     mpython_process_kill(const char* meow_path);
uint32_t mpython_process_ram_kb(const char* meow_path);

// ── KITT C bridge — called from kitt_module.c ─────────────────────────────────
// All functions are thin wrappers around the global KITT object.

// Display
void        c_kitt_text_print(uint8_t row, const char* text);
void        c_kitt_text_clear(void);
uint16_t    c_kitt_display_width(void);
uint16_t    c_kitt_display_height(void);
const char* c_kitt_os_name(void);
const char* c_kitt_device_name(void);

// Input — poll_key returns KITT::generic_key_t cast to int, 0 if no event
int  c_kitt_poll_key(void);
bool c_kitt_poll_key_pressed(void);   // true if last poll_key was a press

// WiFi
bool c_kitt_wifi_connected(void);
void c_kitt_wifi_get_ssid(char* buf, size_t len);
int  c_kitt_wifi_rssi(void);
void c_kitt_wifi_connect(const char* ssid, const char* password);
void c_kitt_wifi_disconnect(void);

// LoRa
bool   c_kitt_lora_enabled(void);
bool   c_kitt_lora_send(const uint8_t* data, size_t len);
bool   c_kitt_lora_available(void);
size_t c_kitt_lora_read(uint8_t* buf, size_t max_len);
int    c_kitt_lora_rssi(void);

// System
uint32_t c_kitt_free_ram_kb(void);
uint32_t c_kitt_uptime_ms(void);
int      c_kitt_battery_percent(void);
int      c_kitt_cpu_mhz(void);

// Notifications
void c_kitt_notify(const char* message);
void c_kitt_popup(const char* title, const char* message, const char* btn_label);

#ifdef __cplusplus
}
#endif
