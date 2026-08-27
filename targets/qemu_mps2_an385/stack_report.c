/*
 * Debug-only task reporting FreeRTOS's real per-task stack high-water
 * marks -- how much of each task's allocated stack has never been
 * touched, the standard way to size a real embedded stack allocation down
 * from a guessed starting point instead of leaving it guessed forever. See
 * docs/ARCHITECTURE.md "Memory footprint" for how to read the output and
 * what it's for.
 *
 * Calls FreeRTOS's task.h directly rather than going through the OSAL,
 * same as freertos_hooks.c: this exists purely because this is a real RTOS
 * target with real stacks to measure, not because the BMS application
 * logic (tasks/, bms/, can/) needs it.
 */
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

/* Long enough to let every app task run several periods and settle into
   its steady-state stack depth before measuring -- the slowest task
   period in this project is 500ms (task_soc), so 3s covers several full
   cycles of all five. */
#define STACK_REPORT_DELAY_MS 3000

/* vTaskListTasks needs one line per task (~40 bytes each per its own doc
   comment in task.h): 5 app tasks + IDLE + this reporting task itself, with
   headroom. */
#define STACK_REPORT_BUFFER_LEN 512

static void stack_report_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(STACK_REPORT_DELAY_MS));

    static char buf[STACK_REPORT_BUFFER_LEN];
    vTaskListTasks(buf, sizeof(buf));

    printf("\n=== Stack high-water marks (words of stack never touched; "
           "see docs/ARCHITECTURE.md \"Memory footprint\") ===\n");
    printf("Name            State  Prio  StackHWM  TaskNum\n");
    printf("%s\n", buf);

    vTaskDelete(NULL);
}

void stack_report_start(void) {
    xTaskCreate(stack_report_task, "stack_report", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
}
