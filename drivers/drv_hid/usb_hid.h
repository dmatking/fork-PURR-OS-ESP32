#pragma once
#include <stdint.h>
#include <stdbool.h>

// HID keyboard report — 6KRO (6-key rollover) + modifier byte
typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} hid_keyboard_report_t;

void usb_hid_init();
void usb_hid_send_report(const hid_keyboard_report_t* report);
void usb_hid_release_all();
bool usb_hid_ready();
