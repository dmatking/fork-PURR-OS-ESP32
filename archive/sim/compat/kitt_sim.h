#pragma once
// kitt_sim.h — minimal KITT stub for simulator builds.
// Replaces ../kitt.h — covers every KITT API called by blackberry_ui.cpp.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

class KITT {
public:
    // ── App list (populated with demo entries in kitt_sim.cpp) ────────────────
    struct app_entry_t {
        char name[48];
        char path[128];
        char icon[48];
    };

    int             app_list_count()                           const { return _napp; }
    void            app_get_entry(int i, app_entry_t* out)    const {
        if (i < _napp) *out = _apps[i];
    }
    void            app_launch(const char*)                          {}
    void            apps_scan()                                      {}
    void            firmware_scan()                                  {}

    // ── Display info ──────────────────────────────────────────────────────────
    uint16_t        display_width()          const { return 320; }
    uint16_t        display_height()         const { return 240; }

    // ── Device info ───────────────────────────────────────────────────────────
    const char*     device_name()            const { return "CYD-SIM"; }
    const char*     os_name()                const { return "PURR OS"; }

    // ── Status ────────────────────────────────────────────────────────────────
    int             battery_percent()        const { return 80; }
    bool            wifi_connected()         const { return true; }
    int             wifi_signal_strength()   const { return -55; }

    // ── Text output (no-op in sim — MiniWin handles drawing) ─────────────────
    void            text_print(uint8_t, const char*) {}
    void            text_clear()                     {}
    void            text_set_color(uint32_t, uint32_t) {}

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void set_memory_warning_cb(void(*)(int)) {}
    void set_crash_report_cb(void(*)(const char*, const char*)) {}

    // ── Internal ─────────────────────────────────────────────────────────────
    void _add_app(const char* name, const char* path) {
        if (_napp < 16) {
            strncpy(_apps[_napp].name, name, 47);
            strncpy(_apps[_napp].path, path, 127);
            _apps[_napp].icon[0] = '\0';
            _napp++;
        }
    }

private:
    app_entry_t _apps[16] = {};
    int         _napp = 0;
};

// Defined in kitt_sim.cpp — populated with demo apps.
extern KITT kitt;
