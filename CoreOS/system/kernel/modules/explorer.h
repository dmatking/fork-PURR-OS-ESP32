#pragma once

// explorer — default PURR OS app launcher shell (explorer.paws).
// Spawns its own FreeRTOS task. Call once from system_task().

#ifdef __cplusplus
extern "C" {
#endif

void explorer_start();

#ifdef __cplusplus
}
#endif
