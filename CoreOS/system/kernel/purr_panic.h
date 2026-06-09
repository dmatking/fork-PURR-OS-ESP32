#pragma once

#include <stdint.h>

/* Kernel Stop Codes */
#define PURR_STOP_DEADBEEF "DEADBEEF"  /* Memory Corruption */
#define PURR_STOP_APP_CRASH "APP_CRASH" /* Application Exception */
#define PURR_STOP_CATFAIL   "CATFAIL"   /* Critical Kernel Failure */
#define PURR_STOP_MEM_FULL  "MEM_FULL"  /* Out of Meow */
#define PURR_STOP_WATCHDOG  "WATCHDOG"  /* HW Watchdog Timeout */
#define PURR_STOP_HAL_FAIL  "HAL_FAIL"  /* Hardware Abstraction Error */

typedef enum {
    PURR_PANIC_BLUE,   /* Recoverable/Warning: :-/ */
    PURR_PANIC_RED     /* Critical/Halt:      :-( */
} purr_panic_level_t;

/**
 * Trigger a System Panic.
 * @param stop_code The symbolic code (e.g. "DEADBEEF")
 * @param level     Blue (Warning) or Red (Critical)
 * @param msg       A short descriptive message
 */
void purr_panic(const char* stop_code, purr_panic_level_t level, const char* msg);