#pragma once
// partition_sim.h — stub for partition_manager.h
#include <stdint.h>
#include <stdbool.h>

static inline void   pm_init()                                         {}
static inline bool   pm_launch(uint8_t)                                { return false; }
static inline bool   pm_delete(uint8_t)                                { return false; }
static inline bool   pm_boot_to_factory()                              { return false; }
static inline int    pm_boot_slot()                                    { return 0; }
static inline bool   pm_sd_available()                                 { return false; }
static inline bool   pm_dump_to_sd(uint8_t, const char*, void*)        { return false; }
