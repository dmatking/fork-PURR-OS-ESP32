#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Desktop background with icons (SD card, etc)
// Handles icon clicks and drawing

void app_desktop_init(void);
void app_desktop_paint(void);
bool app_desktop_touch(int16_t x, int16_t y);

#ifdef __cplusplus
}
#endif
