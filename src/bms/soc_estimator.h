#ifndef SOC_ESTIMATOR_H
#define SOC_ESTIMATOR_H

#include <stdint.h>

/*
 * State of Charge estimator: a scalar Kalman filter fusing two independent
 * SoC signals on every update.
 *
 *   - Prediction: Coulomb counting, integrating measured pack current.
 *     Drifts over long runs from small current-sense errors.
 *   - Measurement: an open-circuit-voltage (OCV) lookup table read against
 *     the average cell voltage. Only accurate at rest -- loaded voltage
 *     includes an IR-drop term that grows with current.
 *
 * Rather than gating the OCV correction on/off at a fixed rest threshold,
 * the measurement's noise is modeled as growing with current^2 (IR drop
 * scales with current, so its error scales with current squared). That
 * makes the standard Kalman gain do the gating continuously: the OCV
 * reading is trusted heavily at rest, mostly ignored under heavy load, and
 * graded in between -- so correction also works while the pack is doing
 * useful work, not only while it happens to be idle.
 */

typedef struct {
    double x_percent;  /* Kalman state: estimated SoC, 0.0-100.0 */
    double p_variance; /* estimate covariance, percent^2 */
    uint16_t capacity_mah;
} soc_estimator_t;

void soc_estimator_init(soc_estimator_t *e, uint16_t capacity_mah);

/* current_ca: pack current in centi-amps, negative = discharge.
   dt_ms: elapsed time since the previous call.
   avg_cell_mv: average per-cell voltage for this interval, used as this
   update's OCV measurement. */
void soc_estimator_update(soc_estimator_t *e, int16_t current_ca, uint32_t dt_ms,
                           uint16_t avg_cell_mv);

/* Returns SoC as percent*2 (0.5% resolution), clamped to [0, 200]. */
uint8_t soc_estimator_get_percent_x2(const soc_estimator_t *e);

/* Returns the filter's estimate covariance as percent^2 * 1000, rounded to
   the nearest integer -- exposed so the Kalman recursion itself (confidence
   shrinking on a trusted measurement, growing during a blind prediction)
   can be unit tested exactly, without a floating-point comparison. */
int32_t soc_estimator_get_variance_x1000(const soc_estimator_t *e);

/* Piecewise-linear OCV(cell mV) -> percent*2 lookup, exposed directly so the
   curve itself can be unit tested independent of the filter. */
uint8_t soc_estimator_ocv_lookup_percent_x2(uint16_t avg_cell_mv);

#endif /* SOC_ESTIMATOR_H */
