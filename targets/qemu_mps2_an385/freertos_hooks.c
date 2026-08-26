/*
 * Hooks FreeRTOSConfig.h enables (configCHECK_FOR_STACK_OVERFLOW=2,
 * configUSE_MALLOC_FAILED_HOOK=1) require the application to provide.
 * Both conditions are fatal for a small BMS ECU (better to halt somewhere
 * obvious in a debugger than run on with a corrupted stack or a task that
 * silently never got its memory), so both just trap.
 */
#include "FreeRTOS.h"
#include "task.h"

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}

void vApplicationMallocFailedHook(void) {
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}
