/*
 * Entry point for the QEMU mps2-an385 target. Wires up the exact same
 * application code the host build uses (tasks/, bms/, can/) against
 * osal_freertos.c instead of osal_posix.c -- this file, uart.c/h,
 * syscalls.c, startup.c, and the linker script are the only things that
 * exist purely because this is a different target, not because the
 * application logic changed at all.
 *
 * Unlike the host build's main.c, this never exits: a real BMS ECU runs
 * until powered off, and vTaskStartScheduler() doesn't return on success
 * anyway. Scenario injection (SCENARIO_OVERVOLTAGE etc.) is a host-CLI
 * convenience with no embedded equivalent here, so this always runs
 * SCENARIO_NOMINAL.
 */
#include <stdio.h>

#include "uart.h"
#include "../../src/osal/osal.h"
#include "../../src/can/can_hal.h"
#include "../../src/tasks/app_context.h"
#include "../../src/tasks/task_sensor.h"
#include "../../src/tasks/task_soc.h"
#include "../../src/tasks/task_fault.h"
#include "../../src/tasks/task_can.h"
#include "../../src/tasks/task_watchdog.h"
#include "../../src/util/logger.h"

static app_context_t g_ctx; /* static, not stack-allocated: this task's stack is small and never returns anyway */

int main(void) {
    uart_init();
    logger_init();

    printf("=== BMS Firmware Simulator (QEMU mps2-an385 / FreeRTOS) ===\n");

    can_hal_init();
    app_context_init(&g_ctx);
    cell_model_set_scenario(SCENARIO_NOMINAL);

    osal_task_create("sensor", task_sensor_main, &g_ctx, 3);
    osal_task_create("soc", task_soc_main, &g_ctx, 2);
    osal_task_create("fault", task_fault_main, &g_ctx, 4);
    osal_task_create("can", task_can_main, &g_ctx, 2);
    osal_task_create("watchdog", task_watchdog_main, &g_ctx, 5);

    osal_start_scheduler(); /* does not return */

    for (;;) { }
    return 0;
}
