#include "logger.h"
#include "../osal/osal.h"

#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *level_str(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default: return "?????";
    }
}

void log_msg(log_level_t level, const char *task, const char *fmt, ...) {
    uint32_t t = osal_get_tick_ms();

    pthread_mutex_lock(&g_log_lock);
    printf("[%6u ms] %s %-8s ", t, level_str(level), task);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    (void)fflush(stdout); /* console logging; a flush failure here isn't actionable */
    pthread_mutex_unlock(&g_log_lock);
}
