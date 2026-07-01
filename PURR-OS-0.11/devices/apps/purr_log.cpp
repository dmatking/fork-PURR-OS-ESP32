#include "purr_log.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

char purr_log_ring[PURR_LOG_LINES][PURR_LOG_LINE_LEN];
int  purr_log_head  = 0;
int  purr_log_count = 0;

static bool           s_hooked = false;
static vprintf_like_t s_prev   = nullptr;

static int log_capture(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    char line[PURR_LOG_LINE_LEN];
    vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    int len = (int)strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
        line[--len] = '\0';
    if (len > 0) {
        strncpy(purr_log_ring[purr_log_head], line, PURR_LOG_LINE_LEN - 1);
        purr_log_ring[purr_log_head][PURR_LOG_LINE_LEN - 1] = '\0';
        purr_log_head = (purr_log_head + 1) % PURR_LOG_LINES;
        if (purr_log_count < PURR_LOG_LINES) purr_log_count++;
    }
    return s_prev ? s_prev(fmt, args) : 0;
}

void purr_log_hook_install(void)
{
    if (s_hooked) return;
    s_prev   = esp_log_set_vprintf(log_capture);
    s_hooked = true;
}
