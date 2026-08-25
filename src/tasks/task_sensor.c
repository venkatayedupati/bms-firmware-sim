#include "task_sensor.h"
#include "../bms/bms_config.h"
#include "../util/logger.h"

void task_sensor_main(void *arg) {
    app_context_t *ctx = (app_context_t *)arg;
    LOGI("sensor", "task started (period=%dms)", TASK_PERIOD_SENSOR_MS);

    while (!osal_is_shutdown_requested()) {
        cell_reading_t reading;
        cell_model_step(TASK_PERIOD_SENSOR_MS, &reading);

        osal_mutex_lock(ctx->lock);
        ctx->latest_reading = reading;
        ctx->hb_sensor_ms = osal_get_tick_ms();
        osal_mutex_unlock(ctx->lock);

        osal_task_delay_ms(TASK_PERIOD_SENSOR_MS);
    }
    LOGI("sensor", "task exiting");
}
