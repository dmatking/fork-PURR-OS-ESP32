#pragma once

// Kernel bootloader shell — OTA slot manager, launched on boot via GPIO or NVS flag.
// Call purr_bootloader_request_reboot() from explorer (or any app) to trigger it next boot.

void purr_bootloader_start();
void purr_bootloader_request_reboot();  // sets NVS flag then esp_restart()
