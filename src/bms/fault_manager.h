#ifndef FAULT_MANAGER_H
#define FAULT_MANAGER_H

#include <stdint.h>
#include "bms_config.h"
#include "cell_model.h"
#include "../can/can_protocol.h"

/*
 * Fault state machine.
 *
 *   NORMAL --(near a limit)--> WARNING --(limit exceeded)--> FAULT
 *      ^                          |                             |
 *      +----(margin restored)-----+                             |
 *                                                                v
 *                                                            SHUTDOWN
 *                                                       (latched; requires
 *                                                        external reset)
 *
 * FAULT is entered on any hard limit violation (overvoltage, undervoltage,
 * overtemp, cell imbalance) or on a watchdog timeout. FAULT auto-clears back
 * to WARNING once every reading is back within limits for a debounce period
 * (hysteresis avoids state chatter at the threshold boundary). Three FAULT
 * entries within FAULT_LATCH_WINDOW_MS latch the pack into SHUTDOWN, modeling
 * a real BMS's "stop trying, open the contactors, wait for service" behavior.
 */

#define FAULT_DEBOUNCE_MS      300
#define FAULT_LATCH_WINDOW_MS  5000
#define FAULT_LATCH_COUNT      3

typedef struct {
    bms_state_t state;
    uint16_t active_faults;   /* FAULT_BIT_* bitmask */
    uint32_t last_ok_tick_ms;
    uint32_t watchdog_fault_until_ms;
    uint32_t fault_entry_ticks_ms[FAULT_LATCH_COUNT];
    int fault_entry_count;
} fault_manager_t;

void fault_manager_init(fault_manager_t *fm);

/* Evaluate one reading at time `now_ms`; updates state and active_faults. */
void fault_manager_evaluate(fault_manager_t *fm, const cell_reading_t *reading,
                             uint32_t now_ms);

/* Called by the watchdog task when a task heartbeat goes stale. */
void fault_manager_report_watchdog_fault(fault_manager_t *fm, uint32_t now_ms);

#endif /* FAULT_MANAGER_H */
