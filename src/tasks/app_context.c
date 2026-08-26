#include "app_context.h"
#include "../bms/bms_config.h"
#include <string.h>

void app_context_init(app_context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->lock = osal_mutex_create();
    cell_model_init();
    soc_estimator_init(&ctx->soc, BMS_PACK_CAPACITY_MAH);
    fault_manager_init(&ctx->fault_mgr);

    /* latest_reading must never be read as all-zero: 0mV isn't a valid cell
       reading, and if the soc task's thread happens to run its first cycle
       before the sensor task publishes its first real one, an all-zero
       voltage reads as "empty" to the OCV lookup -- and at the same time,
       the all-zero current reads as "at rest" to the Kalman filter, which
       is precisely when it trusts the (bogus) voltage most. Seed a sane
       nominal/at-rest default so that race can't manufacture a spurious,
       highly-trusted "pack is empty" measurement. */
    for (int i = 0; i < BMS_CELL_COUNT; i++) {
        ctx->latest_reading.cell_mv[i] = BMS_CELL_NOMINAL_MV;
        ctx->latest_reading.cell_temp_c[i] = 25;
    }
}
