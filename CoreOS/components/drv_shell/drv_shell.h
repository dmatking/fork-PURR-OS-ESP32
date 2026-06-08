#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start the PURR debug shell on UART0 (same port as USB serial / esptool).
// Launches a FreeRTOS task; does not block.
// Output interleaves with ESP_LOG — that is expected and fine for debugging.
void purr_shell_start(void);

#ifdef __cplusplus
}
#endif
