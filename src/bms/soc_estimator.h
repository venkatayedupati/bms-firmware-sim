#ifndef SOC_ESTIMATOR_H
#define SOC_ESTIMATOR_H

#include <stdint.h>

/*
 * State of Charge estimator: a 2-state Kalman filter tracking both the
 * estimated SoC and the current sensor's own calibration bias.
 *
 *   - Prediction: Coulomb counting, integrating measured pack current
 *     *after* subtracting the current best bias estimate. A real
 *     current-sense ADC/shunt has a small constant offset error; left
 *     uncorrected, that offset alone causes unbounded Coulomb-counting
 *     drift no matter how good the rest of the model is -- so it's given
 *     its own state instead of being folded into undifferentiated process
 *     noise.
 *   - Measurement: an open-circuit-voltage (OCV) lookup table read against
 *     the average cell voltage. Observes SoC directly; bias isn't visible
 *     from voltage. Only accurate at rest -- loaded voltage includes an
 *     IR-drop term that grows with current.
 *
 * The two states are coupled through the prediction step (SoC's predicted
 * delta depends on the bias estimate), so repeated OCV corrections let the
 * filter attribute a *persistent* residual to the bias itself rather than
 * repeatedly re-correcting the same drift every cycle. The measurement's
 * noise is modeled as growing with current^2 (IR drop scales with current,
 * so its error scales with current squared), which makes the standard
 * Kalman gain trust OCV heavily at rest, mostly ignore it under heavy load,
 * and grade continuously in between -- no fixed rest threshold anywhere.
 */

typedef struct {
    double soc_percent; /* state x0: estimated SoC, 0.0-100.0 */
    double bias_ca;     /* state x1: estimated current-sensor bias, centiamps
                            (sensor reads current_ca; true current is
                            current_ca - bias_ca) */
    double p00;          /* estimate covariance: soc-soc, percent^2 */
    double p01;          /* estimate covariance: soc-bias, percent*centiamp */
    double p11;          /* estimate covariance: bias-bias, centiamp^2 */
    uint16_t capacity_mah;
} soc_estimator_t;

void soc_estimator_init(soc_estimator_t *e, uint16_t capacity_mah);

/* current_ca: pack current in centi-amps, negative = discharge, as reported
   by the (possibly biased) current sensor.
   dt_ms: elapsed time since the previous call.
   avg_cell_mv: average per-cell voltage for this interval, used as this
   update's OCV measurement. */
void soc_estimator_update(soc_estimator_t *e, int16_t current_ca, uint32_t dt_ms,
                           uint16_t avg_cell_mv);

/* Returns SoC as percent*2 (0.5% resolution), clamped to [0, 200]. */
uint8_t soc_estimator_get_percent_x2(const soc_estimator_t *e);

/* Returns the filter's SoC estimate covariance (p00) as percent^2 * 1000,
   rounded to the nearest integer -- exposed so the Kalman recursion itself
   (confidence shrinking on a trusted measurement, growing during a blind
   prediction) can be unit tested exactly, without a floating-point
   comparison. */
int32_t soc_estimator_get_variance_x1000(const soc_estimator_t *e);

/* Returns the filter's learned current-sensor bias estimate as centiamps *
   10 (0.1 centiamp / 1mA resolution), rounded to the nearest integer. */
int32_t soc_estimator_get_bias_ca_x10(const soc_estimator_t *e);

/* Piecewise-linear OCV(cell mV) -> percent*2 lookup, exposed directly so the
   curve itself can be unit tested independent of the filter. */
uint8_t soc_estimator_ocv_lookup_percent_x2(uint16_t avg_cell_mv);

#endif /* SOC_ESTIMATOR_H */
