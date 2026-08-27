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
 * entries within FAULT_LATCH_WINDOW_MS latch the pack into SHUTDOWN -- unless
 * the fault is severe enough to skip straight there, see bms_asil_t below.
 */

#define FAULT_DEBOUNCE_MS      300
#define FAULT_LATCH_WINDOW_MS  5000
#define FAULT_LATCH_COUNT      3

/*
 * ISO 26262-style Automotive Safety Integrity Level, applied per fault type
 * rather than to the system as a whole (a real hazard analysis would derive
 * these from severity/exposure/controllability for each specific failure
 * mode, not assign one blanket level to "the BMS"). QM ("Quality Managed")
 * is ISO 26262's own term for "no safety integrity level applies".
 *
 * Higher-severity faults don't get the benefit of the 3-strikes latch
 * window real lower-severity faults do: a single ASIL D fault trips
 * SHUTDOWN immediately (see fault_manager_evaluate/report_watchdog_fault).
 * That's the actual point of classifying severity at all here -- a label
 * that didn't change any behavior would just be decoration.
 */
typedef enum {
    BMS_ASIL_QM = 0,
    BMS_ASIL_A  = 1,
    BMS_ASIL_B  = 2,
    BMS_ASIL_C  = 3,
    BMS_ASIL_D  = 4,
} bms_asil_t;

typedef struct {
    bms_state_t state;
    uint16_t active_faults;   /* FAULT_BIT_* bitmask */
    bms_asil_t severity;      /* worst bms_asil_t among active_faults; BMS_ASIL_QM when none are active */
    uint32_t last_ok_tick_ms;
    uint32_t watchdog_fault_until_ms;
    uint32_t fault_entry_ticks_ms[FAULT_LATCH_COUNT];
    int fault_entry_count;
} fault_manager_t;

void fault_manager_init(fault_manager_t *fm);

/* Evaluate one reading at time `now_ms`; updates state, active_faults, and severity. */
void fault_manager_evaluate(fault_manager_t *fm, const cell_reading_t *reading,
                             uint32_t now_ms);

/* Called by the watchdog task when a task heartbeat goes stale. */
void fault_manager_report_watchdog_fault(fault_manager_t *fm, uint32_t now_ms);

/* The worst bms_asil_t among any bits set in a FAULT_BIT_* mask. Exposed
   directly (not just via fault_manager_t.severity) so the classification
   itself -- independent of the state machine's timing/latch logic -- can
   be unit tested on its own. */
bms_asil_t fault_manager_get_severity(uint16_t active_faults);

#endif /* FAULT_MANAGER_H */
