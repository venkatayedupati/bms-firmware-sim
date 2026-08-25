#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include "../osal/osal.h"
#include "../bms/cell_model.h"
#include "../bms/soc_estimator.h"
#include "../bms/fault_manager.h"
#include "../can/can_protocol.h"

/*
 * Shared state between the five tasks, guarded by a single mutex.
 * The sim is small enough (five periodic tasks, sub-millisecond critical
 * sections) that one coarse lock is simpler and just as correct as
 * per-field locking, and it is what most real BMS reference firmware does
 * for the same reason.
 */
typedef struct {
    osal_mutex_t *lock;

    cell_reading_t latest_reading;
    soc_estimator_t soc;
    fault_manager_t fault_mgr;
    bms_status_t status;

    uint32_t hb_sensor_ms;
    uint32_t hb_soc_ms;
    uint32_t hb_fault_ms;
} app_context_t;

void app_context_init(app_context_t *ctx);

#endif /* APP_CONTEXT_H */
