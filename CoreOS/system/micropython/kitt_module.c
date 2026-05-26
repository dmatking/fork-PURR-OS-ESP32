// kitt_module.c — MicroPython C extension: import kitt
// Exposes the KITT hardware API to userland .meow Python apps.
//
// Usage in a .meow app:
//   import kitt
//   kitt.text_clear()
//   kitt.text_print(0, kitt.os_name())
//   key = kitt.poll_key()
//   if key == kitt.KEY_SELECT: ...

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mphal.h"

#include "mpython_runtime.h"  // c_kitt_* bridge functions

// ── Display ───────────────────────────────────────────────────────────────────

STATIC mp_obj_t py_text_print(mp_obj_t row_obj, mp_obj_t text_obj) {
    uint8_t row     = (uint8_t)mp_obj_get_int(row_obj);
    const char* txt = mp_obj_str_get_str(text_obj);
    c_kitt_text_print(row, txt);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(py_text_print_obj, py_text_print);

STATIC mp_obj_t py_text_clear(void) {
    c_kitt_text_clear();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(py_text_clear_obj, py_text_clear);

STATIC mp_obj_t py_display_width(void)  { return mp_obj_new_int(c_kitt_display_width()); }
STATIC mp_obj_t py_display_height(void) { return mp_obj_new_int(c_kitt_display_height()); }
MP_DEFINE_CONST_FUN_OBJ_0(py_display_width_obj,  py_display_width);
MP_DEFINE_CONST_FUN_OBJ_0(py_display_height_obj, py_display_height);

STATIC mp_obj_t py_os_name(void)     { return mp_obj_new_str(c_kitt_os_name(),     strlen(c_kitt_os_name())); }
STATIC mp_obj_t py_device_name(void) { return mp_obj_new_str(c_kitt_device_name(), strlen(c_kitt_device_name())); }
MP_DEFINE_CONST_FUN_OBJ_0(py_os_name_obj,     py_os_name);
MP_DEFINE_CONST_FUN_OBJ_0(py_device_name_obj, py_device_name);

// ── Input ─────────────────────────────────────────────────────────────────────

// poll_key() → int key code (KEY_* constant), 0 if no event pending.
// Check kitt.poll_key_pressed() to distinguish press vs release.
STATIC mp_obj_t py_poll_key(void) {
    return mp_obj_new_int(c_kitt_poll_key());
}
MP_DEFINE_CONST_FUN_OBJ_0(py_poll_key_obj, py_poll_key);

STATIC mp_obj_t py_poll_key_pressed(void) {
    return mp_obj_new_bool(c_kitt_poll_key_pressed());
}
MP_DEFINE_CONST_FUN_OBJ_0(py_poll_key_pressed_obj, py_poll_key_pressed);

// ── WiFi ──────────────────────────────────────────────────────────────────────

STATIC mp_obj_t py_wifi_connected(void) { return mp_obj_new_bool(c_kitt_wifi_connected()); }
MP_DEFINE_CONST_FUN_OBJ_0(py_wifi_connected_obj, py_wifi_connected);

STATIC mp_obj_t py_wifi_ssid(void) {
    char buf[64] = {};
    c_kitt_wifi_get_ssid(buf, sizeof(buf));
    return mp_obj_new_str(buf, strlen(buf));
}
MP_DEFINE_CONST_FUN_OBJ_0(py_wifi_ssid_obj, py_wifi_ssid);

STATIC mp_obj_t py_wifi_rssi(void) { return mp_obj_new_int(c_kitt_wifi_rssi()); }
MP_DEFINE_CONST_FUN_OBJ_0(py_wifi_rssi_obj, py_wifi_rssi);

STATIC mp_obj_t py_wifi_connect(mp_obj_t ssid_obj, mp_obj_t pass_obj) {
    c_kitt_wifi_connect(mp_obj_str_get_str(ssid_obj), mp_obj_str_get_str(pass_obj));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(py_wifi_connect_obj, py_wifi_connect);

STATIC mp_obj_t py_wifi_disconnect(void) { c_kitt_wifi_disconnect(); return mp_const_none; }
MP_DEFINE_CONST_FUN_OBJ_0(py_wifi_disconnect_obj, py_wifi_disconnect);

// ── LoRa ──────────────────────────────────────────────────────────────────────

STATIC mp_obj_t py_lora_enabled(void)   { return mp_obj_new_bool(c_kitt_lora_enabled()); }
STATIC mp_obj_t py_lora_available(void) { return mp_obj_new_bool(c_kitt_lora_available()); }
STATIC mp_obj_t py_lora_rssi(void)      { return mp_obj_new_int(c_kitt_lora_rssi()); }
MP_DEFINE_CONST_FUN_OBJ_0(py_lora_enabled_obj,   py_lora_enabled);
MP_DEFINE_CONST_FUN_OBJ_0(py_lora_available_obj, py_lora_available);
MP_DEFINE_CONST_FUN_OBJ_0(py_lora_rssi_obj,      py_lora_rssi);

// lora_send(bytes) → bool
STATIC mp_obj_t py_lora_send(mp_obj_t data_obj) {
    mp_buffer_info_t info;
    mp_get_buffer_raise(data_obj, &info, MP_BUFFER_READ);
    bool ok = c_kitt_lora_send((const uint8_t*)info.buf, info.len);
    return mp_obj_new_bool(ok);
}
MP_DEFINE_CONST_FUN_OBJ_1(py_lora_send_obj, py_lora_send);

// lora_read() → bytes or None
STATIC mp_obj_t py_lora_read(void) {
    if (!c_kitt_lora_available()) return mp_const_none;
    uint8_t buf[256];
    size_t n = c_kitt_lora_read(buf, sizeof(buf));
    if (n == 0) return mp_const_none;
    return mp_obj_new_bytes(buf, n);
}
MP_DEFINE_CONST_FUN_OBJ_0(py_lora_read_obj, py_lora_read);

// ── System ────────────────────────────────────────────────────────────────────

STATIC mp_obj_t py_free_ram(void)        { return mp_obj_new_int(c_kitt_free_ram_kb()); }
STATIC mp_obj_t py_uptime_ms(void)       { return mp_obj_new_int(c_kitt_uptime_ms()); }
STATIC mp_obj_t py_battery_percent(void) { return mp_obj_new_int(c_kitt_battery_percent()); }
STATIC mp_obj_t py_cpu_mhz(void)         { return mp_obj_new_int(c_kitt_cpu_mhz()); }
MP_DEFINE_CONST_FUN_OBJ_0(py_free_ram_obj,        py_free_ram);
MP_DEFINE_CONST_FUN_OBJ_0(py_uptime_ms_obj,       py_uptime_ms);
MP_DEFINE_CONST_FUN_OBJ_0(py_battery_percent_obj, py_battery_percent);
MP_DEFINE_CONST_FUN_OBJ_0(py_cpu_mhz_obj,         py_cpu_mhz);

// ── Notifications ─────────────────────────────────────────────────────────────

STATIC mp_obj_t py_notify(mp_obj_t msg_obj) {
    c_kitt_notify(mp_obj_str_get_str(msg_obj));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(py_notify_obj, py_notify);

STATIC mp_obj_t py_popup(mp_obj_t title_obj, mp_obj_t msg_obj, mp_obj_t btn_obj) {
    c_kitt_popup(
        mp_obj_str_get_str(title_obj),
        mp_obj_str_get_str(msg_obj),
        mp_obj_str_get_str(btn_obj)
    );
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(py_popup_obj, py_popup);

// ── Module table ──────────────────────────────────────────────────────────────

STATIC const mp_rom_map_elem_t kitt_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_kitt) },

    // Display
    { MP_ROM_QSTR(MP_QSTR_text_print),    MP_ROM_PTR(&py_text_print_obj) },
    { MP_ROM_QSTR(MP_QSTR_text_clear),    MP_ROM_PTR(&py_text_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_width), MP_ROM_PTR(&py_display_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_height),MP_ROM_PTR(&py_display_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_os_name),       MP_ROM_PTR(&py_os_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_device_name),   MP_ROM_PTR(&py_device_name_obj) },

    // Input
    { MP_ROM_QSTR(MP_QSTR_poll_key),         MP_ROM_PTR(&py_poll_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll_key_pressed), MP_ROM_PTR(&py_poll_key_pressed_obj) },

    // Key constants — match KITT::generic_key_t enum values
    { MP_ROM_QSTR(MP_QSTR_KEY_NONE),   MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_KEY_UP),     MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_KEY_DOWN),   MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_KEY_LEFT),   MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_KEY_RIGHT),  MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SELECT), MP_ROM_INT(5) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BACK),   MP_ROM_INT(6) },
    { MP_ROM_QSTR(MP_QSTR_KEY_MENU),   MP_ROM_INT(7) },
    { MP_ROM_QSTR(MP_QSTR_KEY_POWER),  MP_ROM_INT(8) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SOFT1),  MP_ROM_INT(9) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SOFT2),  MP_ROM_INT(10) },

    // WiFi
    { MP_ROM_QSTR(MP_QSTR_wifi_connected),  MP_ROM_PTR(&py_wifi_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_ssid),       MP_ROM_PTR(&py_wifi_ssid_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_rssi),       MP_ROM_PTR(&py_wifi_rssi_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_connect),    MP_ROM_PTR(&py_wifi_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_disconnect), MP_ROM_PTR(&py_wifi_disconnect_obj) },

    // LoRa
    { MP_ROM_QSTR(MP_QSTR_lora_enabled),   MP_ROM_PTR(&py_lora_enabled_obj) },
    { MP_ROM_QSTR(MP_QSTR_lora_available), MP_ROM_PTR(&py_lora_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_lora_rssi),      MP_ROM_PTR(&py_lora_rssi_obj) },
    { MP_ROM_QSTR(MP_QSTR_lora_send),      MP_ROM_PTR(&py_lora_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_lora_read),      MP_ROM_PTR(&py_lora_read_obj) },

    // System
    { MP_ROM_QSTR(MP_QSTR_free_ram),        MP_ROM_PTR(&py_free_ram_obj) },
    { MP_ROM_QSTR(MP_QSTR_uptime_ms),       MP_ROM_PTR(&py_uptime_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_battery_percent), MP_ROM_PTR(&py_battery_percent_obj) },
    { MP_ROM_QSTR(MP_QSTR_cpu_mhz),         MP_ROM_PTR(&py_cpu_mhz_obj) },

    // Notifications
    { MP_ROM_QSTR(MP_QSTR_notify), MP_ROM_PTR(&py_notify_obj) },
    { MP_ROM_QSTR(MP_QSTR_popup),  MP_ROM_PTR(&py_popup_obj) },
};

STATIC MP_DEFINE_CONST_DICT(kitt_module_globals, kitt_module_globals_table);

const mp_obj_module_t kitt_module_obj = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&kitt_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_kitt, kitt_module_obj);
