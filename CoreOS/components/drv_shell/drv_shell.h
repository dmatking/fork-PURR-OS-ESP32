#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start the PURR debug shell on UART0 (same port as USB serial / esptool).
// Launches a FreeRTOS task; does not block.
// Output interleaves with ESP_LOG — that is expected and fine for debugging.
void purr_shell_start(void);

// Execute a single command line synchronously and capture printed output.
// Returns number of bytes written to out_buf (NUL-terminated, never exceeds out_max-1).
// Thread-safe via mutex. Commands that normally block may still block.
int purr_shell_run(const char *line, char *out_buf, size_t out_max);

#ifdef __cplusplus
}
#endif
