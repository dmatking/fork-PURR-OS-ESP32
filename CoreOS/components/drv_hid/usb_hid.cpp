// usb_hid.cpp — USB HID keyboard driver (pure ESP-IDF tinyUSB, no Arduino)
// USB D+/D- are hardware-fixed inside the S2/S3 die.

#include "usb_hid.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tusb_hid_keyboard.h"
#include <string.h>

static const char* TAG = "usb_hid";

static bool s_ready = false;
static hid_keyboard_report_t s_last_report = {};

void usb_hid_init() {
    const tinyusb_config_t cfg = {
        .device_descriptor = NULL,  // use default
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };
    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyUSB install failed: %s", esp_err_to_name(err));
        return;
    }
    s_ready = true;
    ESP_LOGI(TAG, "USB HID keyboard ready");
}

bool usb_hid_ready() {
    return s_ready && tud_mounted();
}

void usb_hid_send_report(const hid_keyboard_report_t* report) {
    if (!usb_hid_ready()) return;
    if (memcmp(report, &s_last_report, sizeof(*report)) == 0) return;
    s_last_report = *report;
    tud_hid_keyboard_report(0, report->modifiers, report->keys);
}

void usb_hid_release_all() {
    hid_keyboard_report_t empty = {};
    usb_hid_send_report(&empty);
}
