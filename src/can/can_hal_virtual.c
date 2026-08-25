#include "can_hal.h"
#include "../osal/osal.h"

#include <stdlib.h>
#include <string.h>

#define MAX_SUBSCRIBERS 8

typedef struct {
    can_rx_callback_t cb;
    void *ctx;
} subscriber_t;

static subscriber_t g_subs[MAX_SUBSCRIBERS];
static int g_sub_count = 0;
static osal_mutex_t *g_bus_lock = NULL;

int can_hal_init(void) {
    g_bus_lock = osal_mutex_create();
    g_sub_count = 0;
    memset(g_subs, 0, sizeof(g_subs));
    return g_bus_lock ? 0 : -1;
}

int can_hal_subscribe(can_rx_callback_t cb, void *ctx) {
    if (g_sub_count >= MAX_SUBSCRIBERS) return -1;
    osal_mutex_lock(g_bus_lock);
    g_subs[g_sub_count].cb = cb;
    g_subs[g_sub_count].ctx = ctx;
    g_sub_count++;
    osal_mutex_unlock(g_bus_lock);
    return 0;
}

int can_hal_send(const can_frame_t *frame) {
    if (!frame || frame->dlc > CAN_MAX_DLC) return -1;

    /* Real CAN arbitration lets the lowest identifier win a bus collision
       and everyone else retransmit; our virtual bus has no real contention
       (delivery is synchronous under the lock) so we model only the
       broadcast-to-all-subscribers semantics, which is what the protocol
       and fault-detection logic actually depend on. */
    osal_mutex_lock(g_bus_lock);
    subscriber_t local_subs[MAX_SUBSCRIBERS];
    int count = g_sub_count;
    memcpy(local_subs, g_subs, sizeof(subscriber_t) * count);
    osal_mutex_unlock(g_bus_lock);

    for (int i = 0; i < count; i++) {
        if (local_subs[i].cb) {
            local_subs[i].cb(frame, local_subs[i].ctx);
        }
    }
    return 0;
}

void can_hal_shutdown(void) {
    g_sub_count = 0;
}
