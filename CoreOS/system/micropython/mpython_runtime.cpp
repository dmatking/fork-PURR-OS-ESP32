#include "mpython_runtime.h"
#include "../kernel/kitt.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

// MicroPython embed headers — available once the micropython component is added.
// Add to idf_component.yml:
//   dependencies:
//     micropython: ">=1.23.0"
// Or clone into components/micropython and set MICROPY_DIR.
#include "port/micropython_embed.h"   // mp_embed_init / mp_embed_exec_str / mp_embed_deinit

extern KITT kitt;

// ── Heap ──────────────────────────────────────────────────────────────────────

// 256 KB internal RAM heap for MicroPython.
// If PSRAM is available and configured, bump this significantly.
static uint8_t mp_heap[256 * 1024];

// ── Process table ─────────────────────────────────────────────────────────────

static constexpr int MAX_PROCESSES = 4;

struct AppProcess {
    char        path[128];
    TaskHandle_t task;
    bool         running;
    uint32_t     heap_base_free;  // free RAM at launch (rough RAM usage delta)
};

static AppProcess procs[MAX_PROCESSES] = {};

static AppProcess* find_proc(const char* path) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (procs[i].running && strcmp(procs[i].path, path) == 0)
            return &procs[i];
    return nullptr;
}

static AppProcess* alloc_proc(const char* path) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!procs[i].running) {
            strncpy(procs[i].path, path, sizeof(procs[i].path) - 1);
            procs[i].running = true;
            procs[i].task = nullptr;
            return &procs[i];
        }
    }
    return nullptr;
}

// ── App task ──────────────────────────────────────────────────────────────────

struct AppTaskArg {
    char path[128];
};

static void app_task(void* arg) {
    AppTaskArg* a = (AppTaskArg*)arg;

    // Read main.py from SPIFFS
    char entry[140];
    snprintf(entry, sizeof(entry), "%s/main.py", a->path);

    File f = SPIFFS.open(entry, "r");
    if (!f) {
        Serial.printf("[mpython] ERR: %s not found\n", entry);
        delete a;
        mpython_process_kill(a->path);
        vTaskDelete(nullptr);
        return;
    }
    String src = f.readString();
    f.close();

    Serial.printf("[mpython] exec: %s\n", entry);
    mp_embed_exec_str(src.c_str());
    Serial.printf("[mpython] done: %s\n", entry);

    // Mark process as no longer running
    AppProcess* p = find_proc(a->path);
    if (p) p->running = false;

    delete a;
    vTaskDelete(nullptr);
}

// ── Runtime lifecycle ─────────────────────────────────────────────────────────

static bool mp_initialized = false;

void mpython_init() {
    if (mp_initialized) return;
    mp_embed_init(mp_heap, sizeof(mp_heap));
    mp_initialized = true;
    Serial.println("[mpython] interpreter ready");
}

void mpython_deinit() {
    if (!mp_initialized) return;
    mp_embed_deinit();
    mp_initialized = false;
}

bool mpython_exec_app(const char* meow_path) {
    if (!mp_initialized) mpython_init();

    if (find_proc(meow_path)) {
        Serial.printf("[mpython] already running: %s\n", meow_path);
        return false;
    }

    AppProcess* p = alloc_proc(meow_path);
    if (!p) {
        Serial.println("[mpython] process table full");
        return false;
    }

    p->heap_base_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    AppTaskArg* arg = new AppTaskArg;
    strncpy(arg->path, meow_path, sizeof(arg->path) - 1);

    BaseType_t ok = xTaskCreatePinnedToCore(
        app_task, "mpython_app", 8192, arg, 2, &p->task, 1
    );

    if (ok != pdPASS) {
        Serial.println("[mpython] ERR: task create failed");
        delete arg;
        p->running = false;
        return false;
    }

    Serial.printf("[mpython] launched: %s\n", meow_path);
    return true;
}

bool mpython_process_running(const char* path) {
    return find_proc(path) != nullptr;
}

void mpython_process_kill(const char* path) {
    AppProcess* p = find_proc(path);
    if (!p) return;
    if (p->task) vTaskDelete(p->task);
    p->running = false;
    p->task    = nullptr;
    Serial.printf("[mpython] killed: %s\n", path);
}

uint32_t mpython_process_ram_kb(const char* path) {
    AppProcess* p = find_proc(path);
    if (!p) return 0;
    uint32_t now  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t used = (p->heap_base_free > now) ? (p->heap_base_free - now) / 1024 : 0;
    return used;
}

// ── KITT C bridge ─────────────────────────────────────────────────────────────

extern "C" {

// Display
void     c_kitt_text_print(uint8_t row, const char* text) { kitt.text_print(row, text); }
void     c_kitt_text_clear(void)                          { kitt.text_clear(); }
uint16_t c_kitt_display_width(void)                       { return kitt.display_width(); }
uint16_t c_kitt_display_height(void)                      { return kitt.display_height(); }
const char* c_kitt_os_name(void)                          { return kitt.os_name(); }
const char* c_kitt_device_name(void)                      { return kitt.device_name(); }

// Input
static bool last_key_pressed = false;
int c_kitt_poll_key(void) {
    KITT::generic_key_t key;
    bool pressed;
    if (kitt.get_key_event(&key, &pressed)) {
        last_key_pressed = pressed;
        return (int)key;
    }
    return 0;
}
bool c_kitt_poll_key_pressed(void) { return last_key_pressed; }

// WiFi
bool c_kitt_wifi_connected(void)                          { return kitt.wifi_connected(); }
void c_kitt_wifi_get_ssid(char* buf, size_t len)          { kitt.wifi_get_connected_ssid(buf, len); }
int  c_kitt_wifi_rssi(void)                               { return kitt.wifi_signal_strength(); }
void c_kitt_wifi_connect(const char* s, const char* p)    { kitt.wifi_connect(s, p); }
void c_kitt_wifi_disconnect(void)                         { kitt.wifi_disconnect(); }

// LoRa
bool   c_kitt_lora_enabled(void)                          { return kitt.lora_enabled(); }
bool   c_kitt_lora_send(const uint8_t* d, size_t l)       { return kitt.lora_send(d, l); }
bool   c_kitt_lora_available(void)                        { return kitt.lora_data_available(); }
size_t c_kitt_lora_read(uint8_t* b, size_t m)             { return kitt.lora_read(b, m); }
int    c_kitt_lora_rssi(void)                             { return kitt.lora_get_rssi(); }

// System
uint32_t c_kitt_free_ram_kb(void) {
    KITT::memory_stats_t m; kitt.memory_get_stats(&m); return m.free_ram_kb;
}
uint32_t c_kitt_uptime_ms(void)    { return (uint32_t)millis(); }
int      c_kitt_battery_percent(void) { return kitt.battery_percent(); }
int      c_kitt_cpu_mhz(void)         { return kitt.cpu_get_freq_mhz(); }

// Notifications
void c_kitt_notify(const char* msg)                              { kitt.log("app", msg); }
void c_kitt_popup(const char* t, const char* m, const char* b)  {
    // Triggers popup_cb if registered, otherwise falls back to text_print
    kitt.text_clear();
    kitt.text_print(0, t);
    kitt.text_print(1, m);
    kitt.text_print(7, b);
}

}  // extern "C"
