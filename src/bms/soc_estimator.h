#ifndef SOC_ESTIMATOR_H
#define SOC_ESTIMATOR_H

#include <stdint.h>

/*
 * Coulomb-counting State of Charge estimator: integrates measured pack
 * current over time against the nominal pack capacity.
 *
 * This is the standard first-pass automotive approach and is what this
 * project implements and tests. It drifts over long runs because small
 * current-sense errors accumulate — the documented next step (see
 * docs/DESIGN.md "Future Work") is to correct that drift periodically
 * against an open-circuit-voltage/SoC lookup table, or replace this with
 * a Kalman filter that fuses both signals.
 */

typedef struct {
    int32_t remaining_mah_x100; /* fixed point, 1/100 mAh, to avoid floats */
    uint16_t capacity_mah;
} soc_estimator_t;

void soc_estimator_init(soc_estimator_t *e, uint16_t capacity_mah);

/* current_ca: pack current in centi-amps, negative = discharge.
   dt_ms: elapsed time since the previous call. */
void soc_estimator_update(soc_estimator_t *e, int16_t current_ca, uint32_t dt_ms);

/* Returns SoC as percent*2 (0.5% resolution), clamped to [0, 200]. */
uint8_t soc_estimator_get_percent_x2(const soc_estimator_t *e);

#endif /* SOC_ESTIMATOR_H */
