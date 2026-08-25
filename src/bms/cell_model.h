#ifndef CELL_MODEL_H
#define CELL_MODEL_H

#include <stdint.h>
#include "bms_config.h"

/*
 * Stand-in for the analog front-end (AFE) that would normally read real
 * cell voltages/temperatures over SPI/I2C (e.g. an LTC6811/BQ76952-class
 * chip). This model produces a plausible discharge curve plus optional
 * injected fault scenarios, so the rest of the firmware can be developed
 * and tested without a physical pack.
 */

typedef enum {
    SCENARIO_NOMINAL = 0,
    SCENARIO_OVERVOLTAGE,
    SCENARIO_UNDERVOLTAGE,
    SCENARIO_OVERTEMP,
    SCENARIO_CELL_IMBALANCE,
} cell_scenario_t;

typedef struct {
    uint16_t cell_mv[BMS_CELL_COUNT];
    int16_t cell_temp_c[BMS_CELL_COUNT];
    int16_t pack_current_ca; /* negative = discharging */
} cell_reading_t;

void cell_model_init(void);
void cell_model_set_scenario(cell_scenario_t scenario);
/* Advances the internal simulation clock by dt_ms and returns a new reading. */
void cell_model_step(uint32_t dt_ms, cell_reading_t *out);

#endif /* CELL_MODEL_H */
