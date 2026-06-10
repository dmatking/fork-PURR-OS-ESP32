#include "mtp_manager.h"
#include "esp_log.h"

static const char *TAG = "mtp";

// USB MTP mode — allows PC to browse and write to SPIFFS/LittleFS over USB.
// Requires USB_MSC or a MTP library (e.g., ESP_MTP or tinyusb MTP class).
// Full implementation depends on tinyusb MTP class availability for ESP32-S3.
// The interface is defined; bring-up is deferred to the tinyusb integration step.

static bool mtp_active = false;

void mtp_manager_init() {
    ESP_LOGI(TAG, "init (tinyusb integration pending)");
}

void mtp_manager_update() {
    // tinyusb task would be driven here
}

void mtp_manager_deinit() {
    if (mtp_active) mtp_manager_exit();
}

bool mtp_manager_active() { return mtp_active; }

void mtp_manager_enter() {
    ESP_LOGI(TAG, "entering MTP mode");
    mtp_active = true;
    // TODO: tinyusb_mtp_start()
}

void mtp_manager_exit() {
    ESP_LOGI(TAG, "exiting MTP mode");
    mtp_active = false;
    // TODO: tinyusb_mtp_stop()
}
