#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Scan /sdcard for .COM and .EXE files and present a simple list UI
// drawn directly into the MiniWin window before any DOS program is loaded.
// Returns the selected path (static buffer) or NULL if user cancelled.
const char *magidos_filepicker_show(void);

#ifdef __cplusplus
}
#endif
