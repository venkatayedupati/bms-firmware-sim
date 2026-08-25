#include "task_soc.h"
#include "../bms/bms_config.h"
#include "../util/logger.h"

void task_soc_main(void *arg) {
    app_context_t *ctx = (app_context_t *)arg;
    LOGI("soc", "task started (period=%dms)", TASK_PERIOD_SOC_MS);

    while (!osal_is_shutdown_requested()) {
        osal_mutex_lock(ctx->lock);
        cell_reading_t reading = ctx->latest_reading;
        soc_estimator_update(&ctx->soc, reading.pack_current_ca, TASK_PERIOD_SOC_MS);

        uint16_t pack_mv = 0;
        for (int i = 0; i < BMS_CELL_COUNT; i++) pack_mv += reading.cell_mv[i];

        ctx->status.pack_voltage_mv = pack_mv;
        ctx->status.pack_current_ca = reading.pack_current_ca;
        ctx->status.soc_percent_x2 = soc_estimator_get_percent_x2(&ctx->soc);
        ctx->status.state = (uint8_t)ctx->fault_mgr.state;
        ctx->hb_soc_ms = osal_get_tick_ms();
        osal_mutex_unlock(ctx->lock);

        osal_task_delay_ms(TASK_PERIOD_SOC_MS);
    }
    LOGI("soc", "task exiting");
}
