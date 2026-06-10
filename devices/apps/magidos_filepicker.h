#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Scans /sdcard for DOS executables (.COM/.EXE) and returns path to the first
// found (or NULL if none). Subsequent calls return cached results.
const char *magidos_filepicker_show(void);

// Navigate the file list: dir = +1 (next) or -1 (prev).
void magidos_filepicker_nav(int dir);

// Return path of the currently selected file.
const char *magidos_filepicker_get_selected(void);

// Number of DOS executables found.
int magidos_filepicker_count(void);

#ifdef __cplusplus
}
#endif
