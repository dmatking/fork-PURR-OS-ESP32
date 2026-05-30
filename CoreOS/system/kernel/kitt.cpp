#include "kitt.h"
#include "device_config.h"
#include "../micropython/mpython_runtime.h"
#include "modules/display_ssd1306.h"
#include "modules/display_ili9488.h"
#include "modules/display_ili9341.h"
#include "modules/wifi_manager.h"
#include "modules/bt_manager.h"
#include "modules/lora_manager.h"
#include "modules/power_manager.h"
#include "modules/touch_mxt336t.h"
#include "modules/touch_xpt2046.h"
#include "modules/pi_manager.h"
#include "modules/mtp_manager.h"
#include "modules/flasher.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <lvgl.h>
#include <nvs_flash.h>
#include <Preferences.h>

// ── Internal state ─────────────────────────────────────────────────────────────

static device_config_t cfg;
static bool kitt_ready       = false;
static bool kitt_flasher     = false;
static bool display_pi_flag  = false;
static char os_name_buf[16]  = "PUR OS";  // upgraded to "PURR OS" if LoRa init succeeds

// Input ring buffer
static constexpr int KEY_BUF_SIZE = 8;
static KITT::raw_key_event_t key_buf[KEY_BUF_SIZE];
static int key_buf_head = 0;
static int key_buf_tail = 0;

// Injected generic key queue
static constexpr int GEN_KEY_BUF_SIZE = 8;
struct GenKeyEvent { KITT::generic_key_t key; bool pressed; };
static GenKeyEvent gen_key_buf[GEN_KEY_BUF_SIZE];
static int gen_buf_head = 0;
static int gen_buf_tail = 0;

// Touch
static KITT::touch_event_t last_touch = {};

// Watchdog heartbeat
static Preferences nvs_prefs;
static uint32_t last_heartbeat_ms = 0;
static uint32_t last_battery_refresh = 0;

// Callbacks
static void (*tray_cb)(const KITT::tray_state_t*)                                = nullptr;
static void (*popup_cb)(const char*, const char*, const char*)                   = nullptr;
static void (*notify_cb)(const char*)                                             = nullptr;
static void (*crash_cb)(const char*, const char*)                                 = nullptr;
static void (*mem_warn_cb)(int)                                                   = nullptr;
static void (*reserved_combo_cb)()                                                = nullptr;

// Button pins from device.json (parsed into simple array)
static constexpr int MAX_BUTTONS = 8;
struct ButtonDef { uint8_t pin; bool last_state; };
static ButtonDef buttons[MAX_BUTTONS];
static int button_count = 0;

// App / firmware lists
static constexpr int MAX_APPS     = 32;
static constexpr int MAX_FIRMWARE = 16;
static KITT::app_entry_t      app_list[MAX_APPS];
static KITT::firmware_entry_t fw_list[MAX_FIRMWARE];
static int app_count = 0;
static int fw_count  = 0;

// Power + select combo tracking (reserved combo)
static bool combo_power_held  = false;
static bool combo_select_held = false;

// ── Display callback stubs called by pi_manager ────────────────────────────────

extern "C" void kitt_display_yield_to_pi() {
    display_pi_flag = true;
    Serial.println("[kitt] display yielded to Pi");
}

extern "C" void kitt_display_reclaim_from_pi() {
    display_pi_flag = false;
    Serial.println("[kitt] display reclaimed from Pi");
}

// ── LVGL keypad input driver ───────────────────────────────────────────────────

static void lvgl_keypad_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (gen_buf_head == gen_buf_tail) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->key   = 0;
        return;
    }
    GenKeyEvent ev = gen_key_buf[gen_buf_tail];
    gen_buf_tail = (gen_buf_tail + 1) % GEN_KEY_BUF_SIZE;
    data->state = ev.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    // Map generic key to LVGL key
    switch (ev.key) {
        case KITT::KEY_UP:     data->key = LV_KEY_UP;     break;
        case KITT::KEY_DOWN:   data->key = LV_KEY_DOWN;   break;
        case KITT::KEY_LEFT:   data->key = LV_KEY_LEFT;   break;
        case KITT::KEY_RIGHT:  data->key = LV_KEY_RIGHT;  break;
        case KITT::KEY_SELECT: data->key = LV_KEY_ENTER;  break;
        case KITT::KEY_BACK:   data->key = LV_KEY_ESC;    break;
        case KITT::KEY_MENU:   data->key = LV_KEY_HOME;   break;
        default:               data->key = 0;              break;
    }
}

// ── GPIO polling ───────────────────────────────────────────────────────────────

static void push_raw_key(uint8_t pin, bool pressed) {
    KITT::raw_key_event_t ev;
    ev.gpio_pin    = pin;
    ev.pressed     = pressed;
    ev.timestamp_ms = millis();
    int next = (key_buf_head + 1) % KEY_BUF_SIZE;
    if (next != key_buf_tail) {
        key_buf[key_buf_head] = ev;
        key_buf_head = next;
    }
}

static void poll_gpio_inputs() {
    for (int i = 0; i < button_count; i++) {
        bool state = !digitalRead(buttons[i].pin);  // active-low buttons
        if (state != buttons[i].last_state) {
            buttons[i].last_state = state;
            push_raw_key(buttons[i].pin, state);
        }
    }
}

// ── Reserved combo detection ───────────────────────────────────────────────────

static void check_reserved_combo(KITT::generic_key_t key, bool pressed) {
    if (key == KITT::KEY_POWER)  combo_power_held  = pressed;
    if (key == KITT::KEY_SELECT) combo_select_held = pressed;
    if (combo_power_held && combo_select_held && reserved_combo_cb)
        reserved_combo_cb();
}

// ── Boot sequence ──────────────────────────────────────────────────────────────

bool KITT::init(const char* device_json_path) {
    Serial.println("[kitt] boot start");

    // Step 2: mount filesystem
    if (!SPIFFS.begin(true)) {
        Serial.println("[kitt] ERR 0x01 SPIFFS mount failed");
        return false;
    }

    // Step 3: parse device.json
    if (!device_config_load(device_json_path, &cfg)) {
        Serial.println("[kitt] ERR 0x01 device.json parse failed");
        emergency_text("PURR OS", "ERR:0x01", "JSON parse fail");
        return false;
    }

    // Step 5: init display
    if (strcmp(cfg.display, "ssd1306") == 0) {
        display_ssd1306_init();
    } else if (strcmp(cfg.display, "ili9488") == 0) {
        lv_init();
        display_ili9488_init();
    } else if (strcmp(cfg.display, "ili9341") == 0) {
        lv_init();
        display_ili9341_init();
    } else {
        Serial.println("[kitt] ERR 0x02 unknown display type");
        return false;
    }

    // Step 7: boot splash or verbose log
    if (cfg.verbose_boot) {
        log("KITT", "boot verbose mode");
    } else {
        show_boot_splash();
    }

    // Step 8: check NVS boot flag for flasher mode
    nvs_prefs.begin("kitt_boot", true);
    bool flash_flag = nvs_prefs.getBool("flash_flag", false);
    nvs_prefs.end();

    if (flash_flag) {
        kitt_flasher = true;
        log("KITT", "FLASHER MODE");
        flasher_run(&cfg);
        // never returns
    }

    // Step 9: WiFi
    if (cfg.wifi) {
        wifi_manager_init();
        log("KITT", "wifi OK");
    }

    // Step 10: BT
    if (cfg.bt) {
        bt_manager_init();
        log("KITT", "bt OK");
    }

    // Step 11: LoRa
    if (cfg.lora) {
        uint32_t freq = (strcmp(cfg.lora_region, "EU") == 0) ? 868000000UL : 915000000UL;
        lora_manager_init(freq, 14);
        if (lora_manager_enabled()) {
            strncpy(os_name_buf, "PURR OS", sizeof(os_name_buf));
            log("KITT", "lora OK — PURR OS");
        } else {
            log("KITT", "ERR:0x03 LoRa init fail — PUR OS");
        }
    }

    // Step 13: Pi manager
    if (cfg.pi_slot) {
        pi_manager_init();
        log("KITT", "pi mgr OK");
    }

    // Step 14: power manager
    power_manager_init(cfg.cpu_max_mhz);
    last_battery_refresh = millis();

    // Register LVGL keypad input device (ssd1306 path — no touch)
    if (strcmp(cfg.display, "ssd1306") == 0) {
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type    = LV_INDEV_TYPE_KEYPAD;
        indev_drv.read_cb = lvgl_keypad_read_cb;
        lv_indev_drv_register(&indev_drv);
    }

    // Step 12: touch
    if (strcmp(cfg.touch, "mxt336t") == 0) {
        touch_mxt336t_init();
        log("KITT", "touch OK");
    } else if (strcmp(cfg.touch, "xpt2046") == 0) {
        touch_xpt2046_init();
        static lv_indev_drv_t touch_drv;
        lv_indev_drv_init(&touch_drv);
        touch_drv.type    = LV_INDEV_TYPE_POINTER;
        touch_drv.read_cb = touch_xpt2046_lvgl_read;
        lv_indev_drv_register(&touch_drv);
        log("KITT", "touch XPT2046 OK");
    }

    // Step 15: MicroPython runtime
    mpython_init();
    log("KITT", "micropython ready");

    // Step 18: write KITT_READY to NVS
    nvs_prefs.begin("kitt_boot", false);
    nvs_prefs.putBool("kitt_ready", true);
    nvs_prefs.end();

    // Step 19: first heartbeat
    last_heartbeat_ms = millis();
    nvs_prefs.begin("kitt_hb", false);
    nvs_prefs.putUInt("kitt_hb", last_heartbeat_ms);
    nvs_prefs.end();

    kitt_ready = true;
    log("KITT", "ready");
    return true;
}

// ── Main update tick ───────────────────────────────────────────────────────────

void KITT::update() {
    uint32_t now = millis();

    // Heartbeat — 500ms interval
    if (now - last_heartbeat_ms >= 500) {
        nvs_prefs.begin("kitt_hb", false);
        nvs_prefs.putUInt("kitt_hb", now);
        nvs_prefs.end();
        last_heartbeat_ms = now;
    }

    // GPIO input polling
    poll_gpio_inputs();

    // Touch polling
    if (strcmp(cfg.touch, "mxt336t") == 0) {
        touch_mxt336t_update();
        mxt_touch_event_t mev;
        if (touch_mxt336t_get_event(&mev)) {
            last_touch.x          = mev.x;
            last_touch.y          = mev.y;
            last_touch.pressed    = mev.pressed;
            last_touch.contact_id = mev.contact_id;
        }
    }

    // WiFi scan polling
    if (cfg.wifi) wifi_manager_update();

    // BT discovery polling
    if (cfg.bt) bt_manager_update();

    // Pi state polling
    if (cfg.pi_slot) pi_manager_update();

    // Battery refresh every 60s
    if (now - last_battery_refresh >= 60000) {
        power_manager_refresh_battery();
        last_battery_refresh = now;

        // Push tray update
        if (tray_cb) {
            tray_state_t ts = {};
            ts.battery_percent    = power_manager_battery_percent();
            ts.battery_voltage_mv = power_manager_battery_voltage_mv();
            ts.wifi_connected     = wifi_manager_connected();
            if (ts.wifi_connected) wifi_manager_get_ssid(ts.wifi_ssid, sizeof(ts.wifi_ssid));
            ts.wifi_rssi   = wifi_manager_rssi();
            ts.bt_enabled  = cfg.bt && bt_manager_enabled();
            ts.lora_enabled = cfg.lora && lora_manager_enabled();
            ts.lora_rssi    = cfg.lora ? lora_manager_rssi() : 0;
            tray_cb(&ts);
        }

        // Memory warning thresholds
        if (mem_warn_cb) {
            uint32_t total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
            uint32_t free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL)  / 1024;
            if (total > 0) {
                int pct = (int)((total - free) * 100 / total);
                if (pct >= 90) mem_warn_cb(pct);
            }
        }
    }
}

// ── Lifecycle ──────────────────────────────────────────────────────────────────

void KITT::shutdown() {
    if (cfg.wifi)   wifi_manager_deinit();
    if (cfg.bt)     bt_manager_deinit();
    if (cfg.lora)   lora_manager_deinit();
    if (cfg.pi_slot) pi_manager_deinit();
    power_manager_deinit();
    kitt_ready = false;
    Serial.println("[kitt] shutdown");
}

void KITT::emergency_text(const char* line1, const char* line2, const char* line3) {
    Serial.printf("[KITT EMERGENCY] %s | %s | %s\n", line1, line2, line3);
    if (strcmp(cfg.display, "ssd1306") == 0) {
        display_ssd1306_clear();
        display_ssd1306_text(0, line1);
        display_ssd1306_text(1, line2);
        display_ssd1306_text(2, line3);
    }
}

bool        KITT::is_ready()          { return kitt_ready; }
bool        KITT::is_in_flasher_mode(){ return kitt_flasher; }
bool        KITT::is_verbose()        { return cfg.verbose_boot; }
const char* KITT::device_name()       { return cfg.device; }
const char* KITT::os_name()           { return os_name_buf; }

// ── Display ───────────────────────────────────────────────────────────────────

uint16_t KITT::display_width()  { return cfg.display_w; }
uint16_t KITT::display_height() { return cfg.display_h; }

void KITT::text_print(uint8_t row, const char* text) {
    if (strcmp(cfg.display, "ssd1306") == 0)
        display_ssd1306_text(row, text);
}

void KITT::text_clear() {
    if (strcmp(cfg.display, "ssd1306") == 0)
        display_ssd1306_clear();
}

void KITT::text_set_color(uint32_t, uint32_t) {
    // SSD1306 is monochrome — no-op; colour support reserved for ILI9488 LVGL path
}

void KITT::show_boot_splash() {
    if (strlen(cfg.boot_splash) == 0) {
        text_print(0, "PURR OS");
        text_print(1, cfg.device);
        return;
    }
    File f = SPIFFS.open(cfg.boot_splash, "r");
    if (!f) { text_print(0, "PURR OS"); return; }
    uint8_t row = 0;
    while (f.available() && row < 8) {
        String line = f.readStringUntil('\n');
        line.trim();
        text_print(row++, line.c_str());
    }
    f.close();
}

void KITT::log(const char* tag, const char* message) {
    Serial.printf("[%s] %s\n", tag, message);
    if (cfg.verbose_boot && cfg.display_w > 128) {
        static uint8_t log_row = 2;
        char buf[48];
        snprintf(buf, sizeof(buf), "[%s] %s", tag, message);
        text_print(log_row % 6 + 2, buf);
        log_row++;
    }
}

void KITT::log_errorf(uint8_t code, const char* fmt, ...) {
    char msg[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    Serial.printf("[KITT ERR 0x%02X] %s\n", code, msg);
}

void KITT::display_yield_to_pi()    { kitt_display_yield_to_pi(); }
void KITT::display_reclaim_from_pi(){ kitt_display_reclaim_from_pi(); }
bool KITT::display_pi_owns()        { return display_pi_flag; }

// ── Input ─────────────────────────────────────────────────────────────────────

bool KITT::get_raw_key_event(raw_key_event_t* out) {
    if (key_buf_head == key_buf_tail) return false;
    *out = key_buf[key_buf_tail];
    key_buf_tail = (key_buf_tail + 1) % KEY_BUF_SIZE;
    return true;
}

bool KITT::get_key_event(generic_key_t* key, bool* pressed) {
    if (gen_buf_head == gen_buf_tail) return false;
    GenKeyEvent ev = gen_key_buf[gen_buf_tail];
    gen_buf_tail = (gen_buf_tail + 1) % GEN_KEY_BUF_SIZE;
    *key     = ev.key;
    *pressed = ev.pressed;
    return true;
}

void KITT::inject_key(generic_key_t key, bool pressed) {
    check_reserved_combo(key, pressed);
    int next = (gen_buf_head + 1) % GEN_KEY_BUF_SIZE;
    if (next != gen_buf_tail) {
        gen_key_buf[gen_buf_head] = {key, pressed};
        gen_buf_head = next;
    }
}

bool KITT::get_touch_event(touch_event_t* out) {
    if (strcmp(cfg.touch, "mxt336t") != 0) return false;
    *out = last_touch;
    return last_touch.pressed;
}

void KITT::set_reserved_combo_callback(void (*cb)()) {
    reserved_combo_cb = cb;
}

// ── WiFi ─────────────────────────────────────────────────────────────────────

void KITT::wifi_enable()                                    { wifi_manager_enable(); }
void KITT::wifi_disable()                                   { wifi_manager_disable(); }
bool KITT::wifi_enabled()                                   { return cfg.wifi && wifi_manager_enabled(); }
void KITT::wifi_scan_start()                                { wifi_manager_scan_start(); }
bool KITT::wifi_scan_done()                                 { return wifi_manager_scan_done(); }
int  KITT::wifi_scan_count()                                { return wifi_manager_scan_count(); }
void KITT::wifi_scan_get_ssid(int i, char* b, size_t l)     { wifi_manager_scan_get_ssid(i, b, l); }
int  KITT::wifi_scan_get_rssi(int i)                        { return wifi_manager_scan_get_rssi(i); }
bool KITT::wifi_scan_get_secured(int i)                     { return wifi_manager_scan_get_secured(i); }
void KITT::wifi_connect(const char* s, const char* p)       { wifi_manager_connect(s, p); }
void KITT::wifi_disconnect()                                { wifi_manager_disconnect(); }
bool KITT::wifi_connected()                                 { return wifi_manager_connected(); }
void KITT::wifi_get_connected_ssid(char* b, size_t l)       { wifi_manager_get_ssid(b, l); }
int  KITT::wifi_signal_strength()                           { return wifi_manager_rssi(); }
void KITT::wifi_forget(const char* s)                       { wifi_manager_forget(s); }
void KITT::wifi_yield()                                     { wifi_manager_yield(); }
void KITT::wifi_reclaim()                                   { wifi_manager_reclaim(); }
bool KITT::wifi_yielded()                                   { return wifi_manager_yielded(); }

// ── Bluetooth ─────────────────────────────────────────────────────────────────

void KITT::bt_enable()                                          { bt_manager_enable(); }
void KITT::bt_disable()                                         { bt_manager_disable(); }
bool KITT::bt_enabled()                                         { return cfg.bt && bt_manager_enabled(); }
int  KITT::bt_paired_count()                                    { return bt_manager_paired_count(); }
void KITT::bt_get_paired_name(int i, char* b, size_t l)         { bt_manager_get_paired_name(i, b, l); }
void KITT::bt_get_paired_addr(int i, char* b, size_t l)         { bt_manager_get_paired_addr(i, b, l); }
bool KITT::bt_device_connected(int i)                           { return bt_manager_device_connected(i); }
void KITT::bt_start_discovery(uint32_t t)                       { bt_manager_start_discovery(t); }
void KITT::bt_stop_discovery()                                  { bt_manager_stop_discovery(); }
bool KITT::bt_discovery_active()                                { return bt_manager_discovery_active(); }
int  KITT::bt_discovered_count()                                { return bt_manager_discovered_count(); }
void KITT::bt_get_discovered_name(int i, char* b, size_t l)     { bt_manager_get_discovered_name(i, b, l); }
void KITT::bt_get_discovered_addr(int i, char* b, size_t l)     { bt_manager_get_discovered_addr(i, b, l); }
void KITT::bt_pair(int i)                                       { bt_manager_pair(i); }
void KITT::bt_unpair(int i)                                     { bt_manager_unpair(i); }
void KITT::bt_yield()                                           { bt_manager_yield(); }
void KITT::bt_reclaim()                                         { bt_manager_reclaim(); }
bool KITT::bt_yielded()                                         { return bt_manager_yielded(); }

// ── LoRa ─────────────────────────────────────────────────────────────────────

void     KITT::lora_enable()                            { lora_manager_reclaim(); }
void     KITT::lora_disable()                           { lora_manager_deinit(); }
bool     KITT::lora_enabled()                           { return cfg.lora && lora_manager_enabled(); }
void     KITT::lora_set_frequency(uint32_t f)           { lora_manager_set_frequency(f); }
void     KITT::lora_set_power(uint8_t d)                { lora_manager_set_power(d); }
void     KITT::lora_set_spreading_factor(uint8_t sf)    { lora_manager_set_spreading_factor(sf); }
void     KITT::lora_set_bandwidth(uint32_t bw)          { lora_manager_set_bandwidth(bw); }
void     KITT::lora_set_coding_rate(uint8_t cr)         { lora_manager_set_coding_rate(cr); }
void     KITT::lora_set_sync_word(uint8_t sw)           { lora_manager_set_sync_word(sw); }
uint32_t KITT::lora_get_frequency()                     { return lora_manager_get_frequency(); }
uint8_t  KITT::lora_get_power()                         { return lora_manager_get_power(); }
int      KITT::lora_get_rssi()                          { return lora_manager_rssi(); }
float    KITT::lora_get_snr()                           { return lora_manager_snr(); }
bool     KITT::lora_send(const uint8_t* d, size_t l)    { return lora_manager_send(d, l); }
bool     KITT::lora_busy()                              { return lora_manager_busy(); }
bool     KITT::lora_data_available()                    { return lora_manager_data_available(); }
size_t   KITT::lora_read(uint8_t* b, size_t l)          { return lora_manager_read(b, l); }
void     KITT::lora_transmit_log(const char*)           {}  // TODO: chunked LoRa log TX
void     KITT::lora_yield()                             { lora_manager_yield(); }
void     KITT::lora_reclaim()                           { lora_manager_reclaim(); }
bool     KITT::lora_yielded()                           { return lora_manager_yielded(); }

// ── Power ─────────────────────────────────────────────────────────────────────

int  KITT::battery_percent()        { return power_manager_battery_percent(); }
int  KITT::battery_voltage_mv()     { return power_manager_battery_voltage_mv(); }
int  KITT::battery_current_ma()     { return power_manager_battery_current_ma(); }
bool KITT::battery_charging()       { return power_manager_battery_charging(); }
void KITT::cpu_set_freq_mhz(int m)  { power_manager_cpu_set_freq(m); }
int  KITT::cpu_get_freq_mhz()       { return power_manager_cpu_get_freq(); }

void KITT::pi_rail_enable()         { if (cfg.pi_slot) pi_manager_power_on(); }
void KITT::pi_rail_disable()        { if (cfg.pi_slot) pi_manager_power_off(); }
bool KITT::pi_rail_enabled()        { return cfg.pi_slot && pi_manager_rail_enabled(); }
bool KITT::pi_handshake_high()      { return cfg.pi_slot && pi_manager_handshake_high(); }

// ── App & firmware scan ───────────────────────────────────────────────────────

void KITT::apps_scan() {
    app_count = 0;
    File root = SPIFFS.open("/apps");
    if (!root || !root.isDirectory()) return;
    File entry = root.openNextFile();
    while (entry && app_count < MAX_APPS) {
        if (entry.isDirectory()) {
            String mpath = String("/apps/") + entry.name() + "/manifest.json";
            File mf = SPIFFS.open(mpath.c_str(), "r");
            if (mf) {
                StaticJsonDocument<512> doc;
                if (deserializeJson(doc, mf) == DeserializationError::Ok) {
                    auto& a = app_list[app_count++];
                    strlcpy(a.name,    doc["name"]    | "", sizeof(a.name));
                    strlcpy(a.version, doc["version"] | "1.0", sizeof(a.version));
                    snprintf(a.path, sizeof(a.path), "/apps/%s", entry.name());
                    a.is_lightweight = doc["is_lightweight"] | false;
                    a.needs_wifi     = doc["needs_wifi"]     | false;
                    a.needs_bt       = doc["needs_bt"]       | false;
                    a.needs_lora     = doc["needs_lora"]     | false;
                    a.min_ram_kb     = doc["min_ram_kb"]     | 64;
                    a.icon_path[0]   = '\0';
                }
                mf.close();
            }
        }
        entry = root.openNextFile();
    }
    Serial.printf("[kitt] apps_scan: %d apps\n", app_count);
}

void KITT::firmware_scan() {
    fw_count = 0;
    // /friends/ not implemented yet — placeholder
    Serial.println("[kitt] firmware_scan: /friends/ not mounted");
}

int  KITT::app_list_count()      { return app_count; }
int  KITT::firmware_list_count() { return fw_count; }

void KITT::app_get_entry(int i, app_entry_t* out) {
    if (i >= 0 && i < app_count) *out = app_list[i];
}

void KITT::firmware_get_entry(int i, firmware_entry_t* out) {
    if (i >= 0 && i < fw_count) *out = fw_list[i];
}

bool KITT::app_launch(const char* path) {
    return mpython_exec_app(path);
}

bool KITT::firmware_launch(const char* path) {
    Serial.printf("[kitt] firmware launch: %s (pending)\n", path);
    return false;
}

void     KITT::process_kill(const char* path)        { mpython_process_kill(path); }
bool     KITT::process_running(const char* path)      { return mpython_process_running(path); }
uint32_t KITT::process_ram_usage_kb(const char* path) { return mpython_process_ram_kb(path); }

// ── Memory ────────────────────────────────────────────────────────────────────

void KITT::memory_get_stats(memory_stats_t* out) {
    out->total_ram_kb          = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    out->free_ram_kb           = heap_caps_get_free_size(MALLOC_CAP_INTERNAL)  / 1024;
    out->psram_total_kb        = heap_caps_get_total_size(MALLOC_CAP_SPIRAM)   / 1024;
    out->psram_free_kb         = heap_caps_get_free_size(MALLOC_CAP_SPIRAM)    / 1024;
    out->largest_free_block_kb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024;
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void KITT::set_tray_update_cb(void (*cb)(const tray_state_t*)) { tray_cb      = cb; }
void KITT::set_popup_cb(void (*cb)(const char*, const char*, const char*)) { popup_cb  = cb; }
void KITT::set_notify_cb(void (*cb)(const char*))              { notify_cb    = cb; }
void KITT::set_crash_report_cb(void (*cb)(const char*, const char*)) { crash_cb = cb; }
void KITT::set_memory_warning_cb(void (*cb)(int))              { mem_warn_cb  = cb; }
