#pragma once
#include "miniwin.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void hal_input_init(void);
void hal_input_tick(void);

// Set the desktop/shell window handle used as fallback key target
void hal_input_set_shell_handle(mw_handle_t h);

// Raw key ring buffer (printable chars + control)
bool    hal_input_key_available(void);
uint8_t hal_input_key_read(void);

void hal_input_notify_touch(void);

#ifdef __cplusplus
}
#endif
