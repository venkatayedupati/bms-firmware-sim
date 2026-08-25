#include "cell_model.h"
#include <string.h>

static cell_scenario_t g_scenario = SCENARIO_NOMINAL;
static uint32_t g_elapsed_ms = 0;
static uint16_t g_base_mv[BMS_CELL_COUNT];

void cell_model_init(void) {
    g_scenario = SCENARIO_NOMINAL;
    g_elapsed_ms = 0;
    for (int i = 0; i < BMS_CELL_COUNT; i++) {
        g_base_mv[i] = BMS_CELL_NOMINAL_MV;
    }
}

void cell_model_set_scenario(cell_scenario_t scenario) {
    g_scenario = scenario;
}

void cell_model_step(uint32_t dt_ms, cell_reading_t *out) {
    g_elapsed_ms += dt_ms;

    /* Slow nominal discharge: ~1mV per second under a light simulated load. */
    uint16_t drift_mv = (uint16_t)(g_elapsed_ms / 1000);

    for (int i = 0; i < BMS_CELL_COUNT; i++) {
        int32_t mv = (int32_t)g_base_mv[i] - drift_mv;
        out->cell_temp_c[i] = 25; /* room temp baseline */

        switch (g_scenario) {
            case SCENARIO_OVERVOLTAGE:
                if (i == 0) mv = BMS_CELL_OVERVOLTAGE_MV + 50;
                break;
            case SCENARIO_UNDERVOLTAGE:
                if (i == 0) mv = BMS_CELL_UNDERVOLTAGE_MV - 50;
                break;
            case SCENARIO_OVERTEMP:
                out->cell_temp_c[i] = (i == 1) ? (BMS_OVERTEMP_C + 5) : 25;
                break;
            case SCENARIO_CELL_IMBALANCE:
                if (i == 2) mv += BMS_CELL_IMBALANCE_MV + 20;
                break;
            case SCENARIO_NOMINAL:
            default:
                break;
        }

        if (mv < 0) mv = 0;
        if (mv > 65535) mv = 65535;
        out->cell_mv[i] = (uint16_t)mv;
    }

    /* Simulated 2A discharge load (200 centi-amps), constant for this demo. */
    out->pack_current_ca = -200;
}
