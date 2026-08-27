#include "fault_manager.h"
#include <string.h>

static void record_fault_entry(fault_manager_t *fm, uint32_t now_ms) {
    if (fm->fault_entry_count < FAULT_LATCH_COUNT) {
        fm->fault_entry_ticks_ms[fm->fault_entry_count++] = now_ms;
    } else {
        memmove(&fm->fault_entry_ticks_ms[0], &fm->fault_entry_ticks_ms[1],
                sizeof(uint32_t) * (FAULT_LATCH_COUNT - 1));
        fm->fault_entry_ticks_ms[FAULT_LATCH_COUNT - 1] = now_ms;
    }
}

static int latch_should_trip(const fault_manager_t *fm, uint32_t now_ms) {
    if (fm->fault_entry_count < FAULT_LATCH_COUNT) return 0;
    uint32_t oldest = fm->fault_entry_ticks_ms[0];
    return (now_ms - oldest) <= FAULT_LATCH_WINDOW_MS;
}

/* Per-fault-type ISO 26262-style severity. Overvoltage/overtemp: thermal
   runaway risk, the headline BMS hazard. Undervoltage: over-discharge cell
   damage, real but less acutely dangerous. Cell imbalance: a longevity/
   efficiency concern, gradual, the least severe hard fault here. Task
   watchdog: loss of the fault monitor itself -- ISO 26262 treats losing a
   safety mechanism at least as seriously as the worst hazard it guards
   against, hence the same top severity as the thermal faults. */
typedef struct {
    uint16_t bit;
    bms_asil_t severity;
} fault_severity_entry_t;

static const fault_severity_entry_t FAULT_SEVERITIES[] = {
    { FAULT_BIT_OVERVOLTAGE,    BMS_ASIL_D },
    { FAULT_BIT_OVERTEMP,       BMS_ASIL_D },
    { FAULT_BIT_TASK_WATCHDOG,  BMS_ASIL_D },
    { FAULT_BIT_UNDERVOLTAGE,   BMS_ASIL_C },
    { FAULT_BIT_CELL_IMBALANCE, BMS_ASIL_B },
};
#define FAULT_SEVERITIES_LEN (sizeof(FAULT_SEVERITIES) / sizeof(FAULT_SEVERITIES[0]))

bms_asil_t fault_manager_get_severity(uint16_t active_faults) {
    bms_asil_t worst = BMS_ASIL_QM;
    for (size_t i = 0; i < FAULT_SEVERITIES_LEN; i++) {
        if ((active_faults & FAULT_SEVERITIES[i].bit) && FAULT_SEVERITIES[i].severity > worst) {
            worst = FAULT_SEVERITIES[i].severity;
        }
    }
    return worst;
}

void fault_manager_init(fault_manager_t *fm) {
    memset(fm, 0, sizeof(*fm));
    fm->state = BMS_STATE_NORMAL;
}

static uint16_t evaluate_hard_faults(const cell_reading_t *r, int *warning_out) {
    uint16_t faults = 0;
    int warning = 0;
    uint16_t min_mv = r->cell_mv[0];
    uint16_t max_mv = r->cell_mv[0];
    int16_t max_temp = r->cell_temp_c[0];

    for (int i = 0; i < BMS_CELL_COUNT; i++) {
        uint16_t mv = r->cell_mv[i];
        if (mv < min_mv) min_mv = mv;
        if (mv > max_mv) max_mv = mv;
        if (r->cell_temp_c[i] > max_temp) max_temp = r->cell_temp_c[i];

        if (mv >= BMS_CELL_OVERVOLTAGE_MV) faults |= FAULT_BIT_OVERVOLTAGE;
        else if (mv >= BMS_CELL_OVERVOLTAGE_MV - BMS_WARNING_MARGIN_MV) warning = 1;

        if (mv <= BMS_CELL_UNDERVOLTAGE_MV) faults |= FAULT_BIT_UNDERVOLTAGE;
        else if (mv <= BMS_CELL_UNDERVOLTAGE_MV + BMS_WARNING_MARGIN_MV) warning = 1;
    }

    if (max_temp >= BMS_OVERTEMP_C) faults |= FAULT_BIT_OVERTEMP;
    else if (max_temp >= BMS_OVERTEMP_C - 5) warning = 1;

    if ((uint16_t)(max_mv - min_mv) >= BMS_CELL_IMBALANCE_MV) faults |= FAULT_BIT_CELL_IMBALANCE;
    else if ((uint16_t)(max_mv - min_mv) >= (BMS_CELL_IMBALANCE_MV * 3 / 4)) warning = 1;

    *warning_out = warning;
    return faults;
}

void fault_manager_evaluate(fault_manager_t *fm, const cell_reading_t *reading,
                             uint32_t now_ms) {
    if (fm->state == BMS_STATE_SHUTDOWN) {
        return; /* latched; a real pack requires a service tool to clear this */
    }

    int warning = 0;
    uint16_t hard_faults = evaluate_hard_faults(reading, &warning);

    int watchdog_active = (fm->watchdog_fault_until_ms != 0) &&
                           (now_ms < fm->watchdog_fault_until_ms);
    uint16_t total_faults = hard_faults | (watchdog_active ? FAULT_BIT_TASK_WATCHDOG : 0);
    fm->active_faults = total_faults;
    fm->severity = fault_manager_get_severity(total_faults);

    if (total_faults != 0) {
        if (fm->state != BMS_STATE_FAULT) {
            record_fault_entry(fm, now_ms);
            fm->state = BMS_STATE_FAULT;
        }
        /* A single BMS_ASIL_D fault trips SHUTDOWN immediately, bypassing
           the 3-strikes latch window entirely -- the most severe hazards
           here (thermal runaway risk, or having lost the fault monitor
           itself) don't get a second chance before requiring service. */
        if (fm->severity >= BMS_ASIL_D || latch_should_trip(fm, now_ms)) {
            fm->state = BMS_STATE_SHUTDOWN;
        }
        fm->last_ok_tick_ms = 0;
    } else if (fm->state == BMS_STATE_FAULT) {
        if (fm->last_ok_tick_ms == 0) {
            fm->last_ok_tick_ms = now_ms;
        } else if (now_ms - fm->last_ok_tick_ms >= FAULT_DEBOUNCE_MS) {
            fm->state = warning ? BMS_STATE_WARNING : BMS_STATE_NORMAL;
            fm->last_ok_tick_ms = 0;
        }
    } else {
        fm->state = warning ? BMS_STATE_WARNING : BMS_STATE_NORMAL;
    }
}

void fault_manager_report_watchdog_fault(fault_manager_t *fm, uint32_t now_ms) {
    if (fm->state == BMS_STATE_SHUTDOWN) return;

    fm->watchdog_fault_until_ms = now_ms + FAULT_DEBOUNCE_MS;
    fm->active_faults |= FAULT_BIT_TASK_WATCHDOG;
    fm->severity = fault_manager_get_severity(fm->active_faults);

    if (fm->state != BMS_STATE_FAULT) {
        record_fault_entry(fm, now_ms);
        fm->state = BMS_STATE_FAULT;
    }
    /* FAULT_BIT_TASK_WATCHDOG is BMS_ASIL_D (see FAULT_SEVERITIES above):
       losing the fault monitor itself never gets a second chance either. */
    if (fm->severity >= BMS_ASIL_D || latch_should_trip(fm, now_ms)) {
        fm->state = BMS_STATE_SHUTDOWN;
    }
}
