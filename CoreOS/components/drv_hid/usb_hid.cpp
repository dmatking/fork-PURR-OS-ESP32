#include "usb_hid.h"
#include <Arduino.h>

// ESP32-S2 native USB HID via Arduino-ESP32 USB stack (tinyUSB wrapper).
// USB D+/D- are hardware-fixed on GPIO19/GPIO20 inside the S2 die.
// The schematic labels them as module pins IO22/IO23 on the WROVER connector —
// these route internally to the same USB pads.

#include "USB.h"
#include "USBHIDKeyboard.h"

static USBHIDKeyboard hid_kb;
static bool hid_ready_flag = false;

static hid_keyboard_report_t last_report = {};

void usb_hid_init() {
    USB.manufacturerName("PURR OS");
    USB.productName("CattoHID Keyboard");
    USB.serialNumber("CATTOHID-001");
    USB.begin();
    hid_kb.begin();
    hid_ready_flag = true;
    Serial.println("[hid] USB HID keyboard started");
}

bool usb_hid_ready() {
    return hid_ready_flag;
}

void usb_hid_send_report(const hid_keyboard_report_t* report) {
    if (!hid_ready_flag) return;

    // Only send if report changed — avoids flooding the USB bus
    if (memcmp(report, &last_report, sizeof(*report)) == 0) return;
    last_report = *report;

    // USBHIDKeyboard exposes sendReport directly
    KeyReport kr;
    kr.modifiers = report->modifiers;
    kr.reserved  = 0;
    memcpy(kr.keys, report->keys, 6);
    hid_kb.sendReport(&kr);
}

void usb_hid_release_all() {
    hid_keyboard_report_t empty = {};
    usb_hid_send_report(&empty);
}
