#include "osal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

struct osal_task {
    pthread_t thread;
    char name[32];
    osal_task_fn fn;
    void *arg;
};

struct osal_queue {
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    uint8_t *buf;
    size_t item_size;
    size_t capacity;
    size_t count;
    size_t head;
};

struct osal_mutex {
    pthread_mutex_t m;
};

static volatile int g_shutdown = 0;
static struct timespec g_start_time;
static pthread_once_t g_clock_once = PTHREAD_ONCE_INIT;

static void clock_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);
}

uint32_t osal_get_tick_ms(void) {
    pthread_once(&g_clock_once, clock_init);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    /* Both diffs must stay signed until combined: tv_nsec can go backward
       across a second boundary even though total time moves forward, and
       widening that negative value to uint64_t before dividing turns it
       into a huge positive number instead of a small negative one. */
    int64_t sec_diff = (int64_t)now.tv_sec - (int64_t)g_start_time.tv_sec;
    int64_t nsec_diff = (int64_t)now.tv_nsec - (int64_t)g_start_time.tv_nsec;
    int64_t ms = sec_diff * 1000 + nsec_diff / 1000000;
    return (uint32_t)ms;
}

static void *thread_trampoline(void *arg) {
    osal_task_t *t = (osal_task_t *)arg;
    t->fn(t->arg);
    return NULL;
}

osal_task_t *osal_task_create(const char *name, osal_task_fn fn, void *arg,
                               int priority) {
    osal_task_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->fn = fn;
    t->arg = arg;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* Best-effort priority hint. On the host this is advisory only (SCHED_OTHER
       does not honor it without elevated privileges); on a real FreeRTOS target
       this maps directly to the task priority passed to xTaskCreate. */
    (void)priority;

    pthread_create(&t->thread, &attr, thread_trampoline, t);
    pthread_attr_destroy(&attr);
    return t;
}

void osal_task_join(osal_task_t *t) {
    if (!t) return;
    pthread_join(t->thread, NULL);
    free(t);
}

void osal_task_delay_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

osal_queue_t *osal_queue_create(size_t item_size, size_t capacity) {
    osal_queue_t *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->buf = calloc(capacity, item_size);
    q->item_size = item_size;
    q->capacity = capacity;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return q;
}

static void abs_deadline(struct timespec *ts, uint32_t timeout_ms) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += timeout_ms / 1000;
    long ns = ts->tv_nsec + (long)(timeout_ms % 1000) * 1000000L;
    ts->tv_sec += ns / 1000000000L;
    ts->tv_nsec = ns % 1000000000L;
}

int osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms) {
    pthread_mutex_lock(&q->lock);
    struct timespec deadline;
    abs_deadline(&deadline, timeout_ms);
    int rc = 0;
    while (q->count == q->capacity) {
        rc = pthread_cond_timedwait(&q->not_full, &q->lock, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&q->lock);
            return 0; /* full, timed out */
        }
    }
    size_t tail = (q->head + q->count) % q->capacity;
    memcpy(q->buf + tail * q->item_size, item, q->item_size);
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 1;
}

int osal_queue_receive(osal_queue_t *q, void *item, uint32_t timeout_ms) {
    pthread_mutex_lock(&q->lock);
    struct timespec deadline;
    abs_deadline(&deadline, timeout_ms);
    int rc = 0;
    while (q->count == 0) {
        rc = pthread_cond_timedwait(&q->not_empty, &q->lock, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&q->lock);
            return 0; /* empty, timed out */
        }
    }
    memcpy(item, q->buf + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return 1;
}

osal_mutex_t *osal_mutex_create(void) {
    osal_mutex_t *m = calloc(1, sizeof(*m));
    pthread_mutex_init(&m->m, NULL);
    return m;
}

void osal_mutex_lock(osal_mutex_t *m) { pthread_mutex_lock(&m->m); }
void osal_mutex_unlock(osal_mutex_t *m) { pthread_mutex_unlock(&m->m); }

void osal_start_scheduler(void) {
    /* Host tasks are already running as pthreads; just block the main
       thread until shutdown is requested (e.g. by the watchdog or a
       fixed sim duration in main.c). */
    while (!g_shutdown) {
        osal_task_delay_ms(50);
    }
}

void osal_request_shutdown(void) { g_shutdown = 1; }
int osal_is_shutdown_requested(void) { return g_shutdown; }
