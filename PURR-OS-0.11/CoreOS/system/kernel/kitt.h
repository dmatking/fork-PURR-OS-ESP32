#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ── Lifecycle ────────────────────────────────────────────────────────────────

enum BootMode {
    BOOT_PURR_OS,
    BOOT_MAGICMAC,
};

class KITT {
public:
    void set_boot_mode(BootMode mode);
    BootMode get_boot_mode() const;

    bool init(const char* device_json_path);
    void update();
    void shutdown();
    void emergency_text(const char* line1, const char* line2, const char* line3);

    bool        is_ready();
    bool        is_in_flasher_mode();
    bool        is_verbose();
    const char* device_name();
    const char* os_name();        // "PURR OS" if LoRa present, "PUR OS" if not

    // ── Display ──────────────────────────────────────────────────────────────
    uint16_t display_width();
    uint16_t display_height();
    void text_print(uint8_t row, const char* text);
    void text_clear();
    void text_set_color(uint32_t fg_hex, uint32_t bg_hex);
    void show_boot_splash();
    void log(const char* tag, const char* message);
    void log_errorf(uint8_t code, const char* fmt, ...);
    void display_yield_to_pi();
    void display_reclaim_from_pi();
    bool display_pi_owns();

    // ── Input ────────────────────────────────────────────────────────────────
    typedef struct {
        uint8_t  gpio_pin;
        bool     pressed;
        uint32_t timestamp_ms;
    } raw_key_event_t;

    typedef enum {
        KEY_NONE = 0,
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
        KEY_SELECT, KEY_BACK, KEY_MENU,
        KEY_POWER,
        KEY_SOFT1, KEY_SOFT2,
        KEY_RESERVED_COMBO
    } generic_key_t;

    bool get_raw_key_event(raw_key_event_t* out);
    bool get_key_event(generic_key_t* key, bool* pressed);
    void inject_key(generic_key_t key, bool pressed);

    typedef struct {
        uint16_t x;
        uint16_t y;
        bool     pressed;
        uint8_t  contact_id;
    } touch_event_t;

    bool get_touch_event(touch_event_t* out);
    void set_reserved_combo_callback(void (*cb)(void));

    // ── WiFi ─────────────────────────────────────────────────────────────────
    void wifi_enable();
    void wifi_disable();
    bool wifi_enabled();
    bool wifi_scan_start();
    bool wifi_scan_done();
    int  wifi_scan_count();
    void wifi_scan_get_ssid(int index, char* buf, size_t len);
    int  wifi_scan_get_rssi(int index);
    bool wifi_scan_get_secured(int index);
    void wifi_connect(const char* ssid, const char* password);
    void wifi_disconnect();
    bool wifi_connected();
    void wifi_get_connected_ssid(char* buf, size_t len);
    int  wifi_signal_strength();
    void wifi_forget(const char* ssid);
    void wifi_yield();
    void wifi_reclaim();
    bool wifi_yielded();

    // ── Bluetooth ────────────────────────────────────────────────────────────
    void bt_enable();
    void bt_disable();
    bool bt_enabled();
    int  bt_paired_count();
    void bt_get_paired_name(int index, char* buf, size_t len);
    void bt_get_paired_addr(int index, char* buf, size_t len);
    bool bt_device_connected(int index);
    void bt_start_discovery(uint32_t timeout_ms);
    void bt_stop_discovery();
    bool bt_discovery_active();
    int  bt_discovered_count();
    void bt_get_discovered_name(int index, char* buf, size_t len);
    void bt_get_discovered_addr(int index, char* buf, size_t len);
    void bt_pair(int discovered_index);
    void bt_unpair(int paired_index);
    void bt_yield();
    void bt_reclaim();
    bool bt_yielded();

    // ── LoRa ─────────────────────────────────────────────────────────────────
    void     lora_enable();
    void     lora_disable();
    bool     lora_enabled();
    void     lora_set_frequency(uint32_t freq_hz);
    void     lora_set_power(uint8_t dbm);
    void     lora_set_spreading_factor(uint8_t sf);
    void     lora_set_bandwidth(uint32_t bw_hz);
    void     lora_set_coding_rate(uint8_t cr);
    void     lora_set_sync_word(uint8_t sw);
    uint32_t lora_get_frequency();
    uint8_t  lora_get_power();
    int      lora_get_rssi();
    float    lora_get_snr();
    bool     lora_send(const uint8_t* data, size_t len);
    bool     lora_busy();
    bool     lora_data_available();
    size_t   lora_read(uint8_t* buf, size_t max_len);
    void     lora_transmit_log(const char* log_path);
    void     lora_yield();
    void     lora_reclaim();
    bool     lora_yielded();

    // ── SD card ──────────────────────────────────────────────────────────────
    bool sd_available();

    // ── Power ────────────────────────────────────────────────────────────────
    int  battery_percent();
    int  battery_voltage_mv();
    int  battery_current_ma();
    bool battery_charging();
    void cpu_set_freq_mhz(int mhz);
    int  cpu_get_freq_mhz();
    void pi_rail_enable();
    void pi_rail_disable();
    bool pi_rail_enabled();
    bool pi_handshake_high();

    // ── App & Firmware ───────────────────────────────────────────────────────
    typedef struct {
        char     name[64];
        char     path[512];
        char     version[16];
        bool     is_lightweight;
        bool     needs_wifi;
        bool     needs_bt;
        bool     needs_lora;
        uint32_t min_ram_kb;
        char     icon_path[128];
    } app_entry_t;

    typedef struct {
        char name[64];
        char path[256];
        char type[16];
        char args[128];
        bool needs_wifi;
        bool needs_bt;
        bool needs_lora;
        bool in_manifest;
    } firmware_entry_t;

    void apps_scan();
    void firmware_scan();
    int  app_list_count();
    int  firmware_list_count();
    void app_get_entry(int index, app_entry_t* out);
    void firmware_get_entry(int index, firmware_entry_t* out);
    bool app_launch(const char* path);
    bool firmware_launch(const char* path);
    void process_kill(const char* path);
    bool process_running(const char* path);
    uint32_t process_ram_usage_kb(const char* path);

    // ── Memory ───────────────────────────────────────────────────────────────
    typedef struct {
        uint32_t total_ram_kb;
        uint32_t free_ram_kb;
        uint32_t psram_total_kb;
        uint32_t psram_free_kb;
        uint32_t largest_free_block_kb;
    } memory_stats_t;

    void memory_get_stats(memory_stats_t* out);

    // ── Callbacks ────────────────────────────────────────────────────────────
    typedef struct {
        int  battery_percent;
        int  battery_voltage_mv;
        bool wifi_connected;
        char wifi_ssid[32];
        int  wifi_rssi;
        bool bt_enabled;
        bool lora_enabled;
        int  lora_rssi;
        char time_str[16];
    } tray_state_t;

    void set_tray_update_cb(void (*cb)(const tray_state_t* state));
    void set_popup_cb(void (*cb)(const char* title, const char* message, const char* btn_label));
    void set_notify_cb(void (*cb)(const char* message));
    void set_crash_report_cb(void (*cb)(const char* app_name, const char* reason));
    void set_memory_warning_cb(void (*cb)(int percent_used));
};
