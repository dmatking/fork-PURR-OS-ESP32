#pragma once

#define PURR_LOG_LINES    12
#define PURR_LOG_LINE_LEN 40

#ifdef __cplusplus
extern "C" {
#endif

extern char purr_log_ring[PURR_LOG_LINES][PURR_LOG_LINE_LEN];
extern int  purr_log_head;
extern int  purr_log_count;

void purr_log_hook_install(void);

#ifdef __cplusplus
}
#endif
