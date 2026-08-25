#ifndef SOC_ESTIMATOR_H
#define SOC_ESTIMATOR_H

#include <stdint.h>

/*
 * State of Charge estimator: Coulomb-counting (integrating measured pack
 * current over time against the nominal pack capacity) as the primary
 * estimate, periodically corrected against an open-circuit-voltage (OCV)
 * lookup table whenever the pack has been at rest (near-zero current) long
 * enough for loaded voltage's IR drop to have settled out.
 *
 * Coulomb counting alone drifts over long runs from small current-sense
 * errors; OCV correction bounds that drift each time the pack rests, which
 * is the standard low-cost fix before reaching for a full Kalman filter
 * fusing both signals (still the documented next step beyond this, see
 * docs/ARCHITECTURE.md "Future work").
 */

typedef struct {
    int32_t remaining_mah_x100; /* fixed point, 1/100 mAh, to avoid floats */
    uint16_t capacity_mah;
    uint32_t rest_accum_ms; /* continuous time spent within the rest current band */
} soc_estimator_t;

void soc_estimator_init(soc_estimator_t *e, uint16_t capacity_mah);

/* current_ca: pack current in centi-amps, negative = discharge.
   dt_ms: elapsed time since the previous call.
   avg_cell_mv: average per-cell voltage for this interval, used to correct
   drift via OCV lookup once the pack has rested long enough. */
void soc_estimator_update(soc_estimator_t *e, int16_t current_ca, uint32_t dt_ms,
                           uint16_t avg_cell_mv);

/* Returns SoC as percent*2 (0.5% resolution), clamped to [0, 200]. */
uint8_t soc_estimator_get_percent_x2(const soc_estimator_t *e);

/* Piecewise-linear OCV(cell mV) -> percent*2 lookup, exposed directly so the
   curve itself can be unit tested independent of the rest-gating logic. */
uint8_t soc_estimator_ocv_lookup_percent_x2(uint16_t avg_cell_mv);

#endif /* SOC_ESTIMATOR_H */
