#ifndef LOGGER_H
#define LOGGER_H

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} log_level_t;

/* Call once at startup, before any task that might log is created, and
   once at shutdown after every such task has exited. */
void logger_init(void);
void logger_shutdown(void);

/* Thread-safe, timestamped (ms since sim start) console logging. */
void log_msg(log_level_t level, const char *task, const char *fmt, ...);

#define LOGD(task, ...) log_msg(LOG_DEBUG, task, __VA_ARGS__)
#define LOGI(task, ...) log_msg(LOG_INFO, task, __VA_ARGS__)
#define LOGW(task, ...) log_msg(LOG_WARN, task, __VA_ARGS__)
#define LOGE(task, ...) log_msg(LOG_ERROR, task, __VA_ARGS__)

#endif /* LOGGER_H */
