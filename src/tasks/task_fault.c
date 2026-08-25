#include "task_fault.h"
#include "../bms/bms_config.h"
#include "../util/logger.h"

void task_fault_main(void *arg) {
    app_context_t *ctx = (app_context_t *)arg;
    LOGI("fault", "task started (period=%dms)", TASK_PERIOD_FAULT_MS);
    bms_state_t last_logged_state = BMS_STATE_NORMAL;

    while (!osal_is_shutdown_requested()) {
        uint32_t now = osal_get_tick_ms();

        osal_mutex_lock(ctx->lock);
        cell_reading_t reading = ctx->latest_reading;
        fault_manager_evaluate(&ctx->fault_mgr, &reading, now);
        bms_state_t state = ctx->fault_mgr.state;
        uint16_t faults = ctx->fault_mgr.active_faults;
        ctx->hb_fault_ms = now;
        osal_mutex_unlock(ctx->lock);

        if (state != last_logged_state) {
            LOGW("fault", "state change -> %d (active_faults=0x%04x)", state, faults);
            last_logged_state = state;
        }

        osal_task_delay_ms(TASK_PERIOD_FAULT_MS);
    }
    LOGI("fault", "task exiting");
}
