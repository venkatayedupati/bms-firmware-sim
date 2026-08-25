#include <stdio.h>
#include <string.h>

#include "osal/osal.h"
#include "can/can_hal.h"
#include "tasks/app_context.h"
#include "tasks/task_sensor.h"
#include "tasks/task_soc.h"
#include "tasks/task_fault.h"
#include "tasks/task_can.h"
#include "tasks/task_watchdog.h"
#include "util/logger.h"

#define SIM_DURATION_MS 8000

static cell_scenario_t parse_scenario(const char *s) {
    if (!s) return SCENARIO_NOMINAL;
    if (strcmp(s, "overvoltage") == 0) return SCENARIO_OVERVOLTAGE;
    if (strcmp(s, "undervoltage") == 0) return SCENARIO_UNDERVOLTAGE;
    if (strcmp(s, "overtemp") == 0) return SCENARIO_OVERTEMP;
    if (strcmp(s, "imbalance") == 0) return SCENARIO_CELL_IMBALANCE;
    return SCENARIO_NOMINAL;
}

int main(int argc, char **argv) {
    const char *scenario_name = (argc > 1) ? argv[1] : "nominal";
    cell_scenario_t scenario = parse_scenario(scenario_name);

    printf("=== BMS Firmware Simulator ===\n");
    printf("scenario: %s | duration: %dms\n\n", scenario_name, SIM_DURATION_MS);

    can_hal_init();

    app_context_t ctx;
    app_context_init(&ctx);
    cell_model_set_scenario(scenario);

    osal_task_t *t_sensor = osal_task_create("sensor", task_sensor_main, &ctx, 3);
    osal_task_t *t_soc = osal_task_create("soc", task_soc_main, &ctx, 2);
    osal_task_t *t_fault = osal_task_create("fault", task_fault_main, &ctx, 4);
    osal_task_t *t_can = osal_task_create("can", task_can_main, &ctx, 2);
    osal_task_t *t_wdog = osal_task_create("watchdog", task_watchdog_main, &ctx, 5);

    osal_task_delay_ms(SIM_DURATION_MS);
    LOGI("main", "sim duration elapsed, requesting shutdown");
    osal_request_shutdown();

    osal_task_join(t_sensor);
    osal_task_join(t_soc);
    osal_task_join(t_fault);
    osal_task_join(t_can);
    osal_task_join(t_wdog);

    osal_mutex_lock(ctx.lock);
    printf("\n=== Final status ===\n");
    printf("pack_voltage_mv=%u pack_current_ca=%d soc=%.1f%% state=%u active_faults=0x%04x\n",
           ctx.status.pack_voltage_mv, ctx.status.pack_current_ca,
           ctx.status.soc_percent_x2 / 2.0, ctx.status.state, ctx.fault_mgr.active_faults);
    osal_mutex_unlock(ctx.lock);

    can_hal_shutdown();
    return 0;
}
