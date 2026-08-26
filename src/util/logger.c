#include "logger.h"
#include "../osal/osal.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

/* Was a raw pthread_mutex_t here, bypassing the OSAL entirely -- harmless
   on the host build but not portable to osal_freertos.c, which has no
   pthread underneath it. Explicit init/shutdown (rather than a lazily
   created mutex) matches how every other subsystem in this codebase is
   started, and avoids a real init race: two task threads racing to create
   the mutex on their first log call. */
static osal_mutex_t *g_log_lock = NULL;

void logger_init(void) {
    g_log_lock = osal_mutex_create();
}

void logger_shutdown(void) {
    osal_mutex_destroy(g_log_lock);
    g_log_lock = NULL;
}

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

    osal_mutex_lock(g_log_lock);
    /* PRIu32, not a hardcoded %u: uint32_t is "unsigned int" on the host
       build's ABI but "unsigned long" on this project's ARM embedded
       target, and %u silently mismatching the latter is exactly the kind
       of thing that only surfaces once you actually cross-compile. */
    printf("[%6" PRIu32 " ms] %s %-8s ", t, level_str(level), task);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    (void)fflush(stdout); /* console logging; a flush failure here isn't actionable */
    osal_mutex_unlock(g_log_lock);
}
