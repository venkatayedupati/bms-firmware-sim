/*
 * FreeRTOS backend for the OSAL, exercised against a real FreeRTOS kernel
 * (vendored at third_party/FreeRTOS-Kernel) running on an emulated Cortex-M3
 * (QEMU's mps2-an385 machine) -- see targets/qemu_mps2_an385/ and
 * docs/ARCHITECTURE.md "Portability". Nothing outside src/osal/ changes:
 * tasks/, bms/, and can/ are the exact same source files the host build
 * links.
 */
#include "osal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <stdatomic.h>
#include <string.h>

struct osal_task {
    TaskHandle_t handle;
    SemaphoreHandle_t done_sem; /* given by the trampoline right before it
                                    deletes itself; osal_task_join waits on
                                    it, standing in for pthread_join */
    osal_task_fn fn;
    void *arg;
};

struct osal_queue {
    QueueHandle_t handle;
};

struct osal_mutex {
    SemaphoreHandle_t handle;
};

/* Single Cortex-M3 core, no SMP: a plain aligned 32-bit load/store is
   already atomic at the hardware level once a task or ISR isn't
   interrupted mid-instruction, which ARMv7-M guarantees. atomic_int is
   used anyway for consistency with osal_posix.c and because it costs
   nothing extra here (compiles down to the same plain load/store). */
static atomic_int g_shutdown = 0;

static void task_trampoline(void *param) {
    osal_task_t *t = (osal_task_t *)param;
    t->fn(t->arg);
    xSemaphoreGive(t->done_sem);
    vTaskDelete(NULL); /* FreeRTOS task functions must never return */
}

osal_task_t *osal_task_create(const char *name, osal_task_fn fn, void *arg, int priority) {
    osal_task_t *t = pvPortMalloc(sizeof(*t));
    if (!t) return NULL;
    t->fn = fn;
    t->arg = arg;

    t->done_sem = xSemaphoreCreateBinary();
    if (!t->done_sem) {
        vPortFree(t);
        return NULL;
    }

    /* +1: this project's priorities (2-5) are advisory-only on the host's
       SCHED_OTHER pthreads, but mean real preemptive scheduling here, so
       they're passed straight through, just shifted above tskIDLE_PRIORITY
       (0) rather than colliding with it. */
    UBaseType_t rtos_priority = (UBaseType_t)(priority + 1);

    BaseType_t ok = xTaskCreate(task_trampoline, name, configMINIMAL_STACK_SIZE * 2,
                                 t, rtos_priority, &t->handle);
    if (ok != pdPASS) {
        vSemaphoreDelete(t->done_sem);
        vPortFree(t);
        return NULL;
    }
    return t;
}

void osal_task_join(osal_task_t *t) {
    if (!t) return;
    xSemaphoreTake(t->done_sem, portMAX_DELAY);
    vSemaphoreDelete(t->done_sem);
    vPortFree(t);
}

void osal_task_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t osal_get_tick_ms(void) {
    /* configTICK_RATE_HZ is 1000 (see FreeRTOSConfig.h), so portTICK_PERIOD_MS
       is exactly 1 and this is a 1:1 tick-to-millisecond count, matching the
       host backend's millisecond-granularity clock. */
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

osal_queue_t *osal_queue_create(size_t item_size, size_t capacity) {
    osal_queue_t *q = pvPortMalloc(sizeof(*q));
    if (!q) return NULL;
    q->handle = xQueueCreate((UBaseType_t)capacity, (UBaseType_t)item_size);
    if (!q->handle) {
        vPortFree(q);
        return NULL;
    }
    return q;
}

int osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms) {
    return (xQueueSend(q->handle, item, pdMS_TO_TICKS(timeout_ms)) == pdPASS) ? 1 : 0;
}

int osal_queue_receive(osal_queue_t *q, void *item, uint32_t timeout_ms) {
    return (xQueueReceive(q->handle, item, pdMS_TO_TICKS(timeout_ms)) == pdPASS) ? 1 : 0;
}

osal_mutex_t *osal_mutex_create(void) {
    osal_mutex_t *m = pvPortMalloc(sizeof(*m));
    if (!m) return NULL;
    m->handle = xSemaphoreCreateMutex();
    if (!m->handle) {
        vPortFree(m);
        return NULL;
    }
    return m;
}

void osal_mutex_lock(osal_mutex_t *m) { xSemaphoreTake(m->handle, portMAX_DELAY); }
void osal_mutex_unlock(osal_mutex_t *m) { xSemaphoreGive(m->handle); }

void osal_mutex_destroy(osal_mutex_t *m) {
    if (!m) return;
    vSemaphoreDelete(m->handle);
    vPortFree(m);
}

void osal_start_scheduler(void) {
    vTaskStartScheduler(); /* does not return on success */
}

void osal_request_shutdown(void) { atomic_store(&g_shutdown, 1); }
int osal_is_shutdown_requested(void) { return atomic_load(&g_shutdown); }
