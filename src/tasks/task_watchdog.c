#include "task_watchdog.h"
#include "../bms/bms_config.h"
#include "../util/logger.h"

/*
 * Monitors the heartbeat each other task stamps every time it completes a
 * loop iteration. A stale heartbeat means a task is hung, deadlocked, or
 * crashed silently -- exactly the failure mode a real automotive watchdog
 * timer (independent hardware WDT on an MCU) exists to catch, forcing the
 * pack into a fail-safe state rather than continuing to run on stale data.
 */
void task_watchdog_main(void *arg) {
    app_context_t *ctx = (app_context_t *)arg;
    LOGI("watchdog", "task started (period=%dms, stale_threshold=%dms)",
         TASK_PERIOD_WATCHDOG_MS, WATCHDOG_STALE_MS);

    /* Give the other tasks one full period to post their first heartbeat
       before the watchdog starts judging staleness. */
    osal_task_delay_ms(TASK_PERIOD_SENSOR_MS * 2);

    while (!osal_is_shutdown_requested()) {
        uint32_t now = osal_get_tick_ms();

        osal_mutex_lock(ctx->lock);
        uint32_t hb_sensor = ctx->hb_sensor_ms;
        uint32_t hb_soc = ctx->hb_soc_ms;
        uint32_t hb_fault = ctx->hb_fault_ms;
        osal_mutex_unlock(ctx->lock);

        int stale = (now - hb_sensor > WATCHDOG_STALE_MS) ||
                    (now - hb_soc > WATCHDOG_STALE_MS) ||
                    (now - hb_fault > WATCHDOG_STALE_MS);

        if (stale) {
            LOGE("watchdog", "stale task heartbeat detected -- forcing fault state");
            osal_mutex_lock(ctx->lock);
            fault_manager_report_watchdog_fault(&ctx->fault_mgr, now);
            osal_mutex_unlock(ctx->lock);
        }

        osal_task_delay_ms(TASK_PERIOD_WATCHDOG_MS);
    }
    LOGI("watchdog", "task exiting");
}
