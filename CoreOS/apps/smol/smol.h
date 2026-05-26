#pragma once

// Text-mode shell for 128×64 OLED displays (Heltec V3 / SSD1306).
// Call smol_start() once from system_task(); it spawns its own FreeRTOS task.
void smol_start();
