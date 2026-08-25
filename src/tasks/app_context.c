#include "app_context.h"
#include "../bms/bms_config.h"
#include <string.h>

void app_context_init(app_context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->lock = osal_mutex_create();
    cell_model_init();
    soc_estimator_init(&ctx->soc, BMS_PACK_CAPACITY_MAH);
    fault_manager_init(&ctx->fault_mgr);
}
