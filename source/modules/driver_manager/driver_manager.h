#pragma once
// driver_manager.h — PURR OS driver manager public API
//
// The driver manager is a .purr system module that:
//   - Scans configured paths for .purr driver modules
//   - Validates kernel_min/max and chip compatibility
//   - Runs required_catcalls compat check when kernel > kernel_max
//   - Calls each driver's init() and registers its catcalls
//   - Tracks per-driver status badges for the UI

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DRV_STATUS_OK     = 0,  // fully validated, within version range
    DRV_STATUS_COMPAT = 1,  // beyond kernel_max, but all required_catcalls present
    DRV_STATUS_FAIL   = 2,  // required_catcall missing — driver did not load
    DRV_STATUS_SKIP   = 3,  // skipped (wrong chip, IDF version, etc.)
} drv_status_t;

typedef struct {
    char         name[32];
    char         version[12];
    char         type[16];          // "display", "touch", "input", "radio", "gps"
    drv_status_t status;
    char         fail_reason[64];   // populated on DRV_STATUS_FAIL
} drv_entry_t;

// Called at boot — scans paths and loads all drivers found
int  driver_manager_init(void);
void driver_manager_deinit(void);

// Get the loaded driver list (for UI / driver status screen)
int              driver_manager_get_count(void);
const drv_entry_t *driver_manager_get_entry(int idx);

// Status badge string for UI: "[OK]", "[COMPAT]", "[FAIL]", "[SKIP]"
const char *drv_status_badge(drv_status_t s);

#ifdef __cplusplus
}
#endif
