#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*
 * Minimal FreeRTOSConfig.h for this target: an emulated Cortex-M3 (QEMU
 * mps2-an385). Values for the clock rate, priority count, and stack/heap
 * sizes match FreeRTOS's own official CORTEX_MPS2_QEMU_IAR_GCC demo for
 * this exact machine, since those are proven-correct for it rather than
 * guessed.
 */

#define configUSE_PREEMPTION                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                   0
#define configCPU_CLOCK_HZ                       ( ( unsigned long ) 25000000 )
#define configTICK_RATE_HZ                       ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                     ( 9 )
#define configMINIMAL_STACK_SIZE                 ( ( unsigned short ) 128 )
#define configTOTAL_HEAP_SIZE                    ( ( size_t ) ( 32 * 1024 ) )
#define configMAX_TASK_NAME_LEN                  ( 16 )
#define configUSE_16_BIT_TICKS                    0
#define configIDLE_SHOULD_YIELD                   1
#define configUSE_MUTEXES                         1
#define configUSE_RECURSIVE_MUTEXES               0
#define configUSE_COUNTING_SEMAPHORES             1
#define configQUEUE_REGISTRY_SIZE                 0
#define configUSE_QUEUE_SETS                      0
#define configUSE_TIME_SLICING                    1
#define configCHECK_FOR_STACK_OVERFLOW             2
#define configUSE_MALLOC_FAILED_HOOK               1
#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                        0
#define configGENERATE_RUN_TIME_STATS              0

/* Software timers -- unused by this project's tasks, kept minimal. */
#define configUSE_TIMERS                          0

/* Co-routines -- unused, legacy FreeRTOS feature. */
#define configUSE_CO_ROUTINES                     0

#define configSUPPORT_STATIC_ALLOCATION            0
#define configSUPPORT_DYNAMIC_ALLOCATION           1

/* Optional API inclusions actually used (task join needs vTaskDelete,
   queues need send/receive with a timeout). */
#define INCLUDE_vTaskDelete                       1
#define INCLUDE_vTaskDelay                        1
#define INCLUDE_xTaskGetTickCount                 1
#define INCLUDE_uxTaskPriorityGet                 0
#define INCLUDE_vTaskPrioritySet                  0
#define INCLUDE_eTaskGetState                     0
#define INCLUDE_xTaskGetSchedulerState            0
#define INCLUDE_xTaskGetIdleTaskHandle             0
#define INCLUDE_vTaskCleanUpResources              0
#define INCLUDE_vTaskSuspend                      0
#define INCLUDE_xTaskGetCurrentTaskHandle          0

/* Cortex-M3 interrupt priority configuration -- standard values for this
   port; see portable/GCC/ARM_CM3/portmacro.h for what these mean. */
#define configPRIO_BITS                           8
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY    0xff
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 0x20
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

#endif /* FREERTOS_CONFIG_H */
