#ifndef OSAL_H
#define OSAL_H

/*
 * OS Abstraction Layer.
 *
 * The application (tasks/, bms/, can/) is written entirely against this
 * interface, never against pthreads or FreeRTOS directly. The host build
 * links osal_posix.c, which implements every call with pthreads so the
 * whole system runs and is testable on a laptop with no hardware.
 *
 * To port to a real ECU: implement this same header against FreeRTOS
 * (xTaskCreate, xQueueSend, xSemaphoreTake, ...) in osal_freertos.c and
 * relink. No file outside src/osal/ changes. See docs/ARCHITECTURE.md
 * "Portability" section for the exact FreeRTOS call mapping.
 */

#include <stdint.h>
#include <stddef.h>

typedef void (*osal_task_fn)(void *arg);

typedef struct osal_task osal_task_t;
typedef struct osal_queue osal_queue_t;
typedef struct osal_mutex osal_mutex_t;

/* Tasks */
osal_task_t *osal_task_create(const char *name, osal_task_fn fn, void *arg,
                               int priority);
void osal_task_join(osal_task_t *task);
void osal_task_delay_ms(uint32_t ms);
uint32_t osal_get_tick_ms(void);

/* Queues (bounded, blocking) */
osal_queue_t *osal_queue_create(size_t item_size, size_t capacity);
int osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms);
int osal_queue_receive(osal_queue_t *q, void *item, uint32_t timeout_ms);

/* Mutexes */
osal_mutex_t *osal_mutex_create(void);
void osal_mutex_lock(osal_mutex_t *m);
void osal_mutex_unlock(osal_mutex_t *m);
void osal_mutex_destroy(osal_mutex_t *m);

/* Scheduler lifecycle */
void osal_start_scheduler(void);   /* blocks until osal_request_shutdown() */
void osal_request_shutdown(void);
int  osal_is_shutdown_requested(void);

#endif /* OSAL_H */
