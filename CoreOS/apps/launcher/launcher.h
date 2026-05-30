#pragma once

// PURR OS Launcher — resident firmware manager UI for CYD (320×240 touch).
// Call launcher_start() once from system_task(); it runs its own FreeRTOS task.
void launcher_start();
