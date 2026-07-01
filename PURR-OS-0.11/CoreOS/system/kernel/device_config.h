#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char     device[32];
    char     display[16];
    uint16_t display_w;
    uint16_t display_h;
    char     touch[16];
    bool     psram;
    uint8_t  flash_mb;
    uint16_t ram_kb;
    uint8_t  psram_mb;
    bool     pi_slot;
    bool     wifi;
    bool     bt;
    bool     lora;
    bool     cc1101;
    bool     sd;
    uint16_t cpu_max_mhz;
    char     lora_region[8];
    bool     verbose_boot;
    char     boot_splash[64];
    char     keymap[64];
    uint32_t friends_ram_threshold_kb;
} device_config_t;

bool device_config_load(const char* path, device_config_t* out);

// Fill *out with compile-time defaults for the current target.
// Returns false if no compile-time default is defined for this target.
bool device_config_default(device_config_t* out);
