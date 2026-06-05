#pragma once
#include <stdint.h>

// Factory-partition bootloader shell — generic OTA slot manager.
// Launched automatically when the factory image boots (PURR_IS_BOOTLOADER_IMG).
// To trigger a reboot to factory from the full OS, call pm_boot_to_factory()
// in partition_manager.h instead.

// sos      — true if a crash loop was detected; opens SOS screen instead of home
// boot_tries — number of consecutive failed boots (shown in SOS UI)
void purr_bootloader_start(bool sos = false, uint8_t boot_tries = 0);
