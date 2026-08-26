#include "soc_estimator.h"
#include "bms_config.h"
#include <stddef.h>

/* Piecewise-linear approximation of a typical Li-ion resting discharge
   curve: flatter through the middle, steeper near the empty/full ends. Only
   a valid OCV sample at rest -- how much the filter trusts it under load is
   handled by soc_estimator_update's measurement-noise model, not by this
   table. */
typedef struct {
    uint16_t mv;
    uint8_t percent_x2;
} ocv_point_t;

static const ocv_point_t OCV_TABLE[] = {
    {3000,   0},
    {3300,  15},
    {3400,  30},
    {3500,  50},
    {3600,  70},
    {3700, 100},
    {3800, 120},
    {3900, 140},
    {4000, 160},
    {4100, 180},
    {4200, 200},
};
#define OCV_TABLE_LEN (sizeof(OCV_TABLE) / sizeof(OCV_TABLE[0]))

void soc_estimator_init(soc_estimator_t *e, uint16_t capacity_mah) {
    e->capacity_mah = capacity_mah;
    e->soc_percent = 100.0; /* start at 100% */
    e->bias_ca = 0.0;       /* no known current-sensor bias yet */
    e->p00 = BMS_SOC_INITIAL_VARIANCE_PCT2;
    e->p01 = 0.0;           /* SoC and bias start uncorrelated */
    e->p11 = BMS_SOC_BIAS_INITIAL_VARIANCE_CA2;
}

uint8_t soc_estimator_ocv_lookup_percent_x2(uint16_t avg_cell_mv) {
    if (avg_cell_mv <= OCV_TABLE[0].mv) return OCV_TABLE[0].percent_x2;
    if (avg_cell_mv >= OCV_TABLE[OCV_TABLE_LEN - 1].mv) {
        return OCV_TABLE[OCV_TABLE_LEN - 1].percent_x2;
    }

    for (size_t i = 0; i + 1 < OCV_TABLE_LEN; i++) {
        uint16_t lo_mv = OCV_TABLE[i].mv;
        uint16_t hi_mv = OCV_TABLE[i + 1].mv;
        if (avg_cell_mv >= lo_mv && avg_cell_mv <= hi_mv) {
            int32_t lo_p = OCV_TABLE[i].percent_x2;
            int32_t hi_p = OCV_TABLE[i + 1].percent_x2;
            int32_t span_mv = (int32_t)hi_mv - (int32_t)lo_mv;
            int32_t offset_mv = (int32_t)avg_cell_mv - (int32_t)lo_mv;
            return (uint8_t)(lo_p + ((hi_p - lo_p) * offset_mv) / span_mv);
        }
    }
    return 0; /* unreachable: table is exhaustive over uint16_t range via the clamps above */
}

void soc_estimator_update(soc_estimator_t *e, int16_t current_ca, uint32_t dt_ms,
                           uint16_t avg_cell_mv) {
    /* --- Predict --- */
    /* k converts a centiamp held for dt_ms into a fraction of pack capacity,
       i.e. percent per centiamp for this interval (same Coulomb-counting
       arithmetic as before, just factored out so it can scale the bias
       term identically). Integrate current *after* subtracting the current
       best bias estimate. */
    double k = (double)dt_ms / (3600.0 * (double)e->capacity_mah);
    double corrected_current_ca = (double)current_ca - e->bias_ca;
    double delta_percent = k * corrected_current_ca;

    double soc_pred = e->soc_percent + delta_percent;
    double bias_pred = e->bias_ca; /* modeled as constant between updates */

    /* Covariance predict: P_pred = F P F^T + Q, where
       F = [[1, -k], [0, 1]]
       (raising the bias estimate by db lowers the corrected current, and
       hence the SoC delta, by k*db -- this is what couples the two states
       and lets a persistent residual teach the filter about bias instead of
       just being fought every cycle). */
    double p00 = e->p00 - 2.0 * k * e->p01 + k * k * e->p11;
    double p01 = e->p01 - k * e->p11;
    double p11 = e->p11;

    /* Process noise: a larger integrated move carries proportionally more
       per-sample sensor noise (persistent offset is handled by the bias
       state, not this term). A small floor keeps the filter able to
       correct a stale SoC estimate even across zero-current updates. The
       bias is allowed to drift slowly too, since a real calibration offset
       can vary a little with temperature/age even if it's never truly
       constant. */
    double sense_error = BMS_SOC_CURRENT_SENSE_ERROR_FRAC * delta_percent;
    p00 += BMS_SOC_PROCESS_NOISE_FLOOR_PCT2 + sense_error * sense_error;
    p11 += BMS_SOC_BIAS_RANDOM_WALK_CA2_PER_S * ((double)dt_ms / 1000.0);

    /* --- Update: fuse against this cycle's OCV measurement. The
       measurement observes SoC only (H = [1, 0]); noise grows with
       current^2, since IR drop grows with current and its contribution to
       voltage error grows with the square of that. --- */
    double z_percent = (double)soc_estimator_ocv_lookup_percent_x2(avg_cell_mv) / 2.0;
    double current_a = (double)current_ca / 100.0;
    double measurement_noise =
        BMS_SOC_OCV_BASE_VARIANCE_PCT2 + BMS_SOC_OCV_LOAD_COEFF_PCT2_PER_A2 * current_a * current_a;

    double s = p00 + measurement_noise;
    double k_soc = p00 / s;  /* Kalman gain onto SoC */
    double k_bias = p01 / s; /* Kalman gain onto bias, via their coupling */
    double residual = z_percent - soc_pred;

    double soc_new = soc_pred + k_soc * residual;
    double bias_new = bias_pred + k_bias * residual;

    /* P_new = (I - K H) P_pred; this simplified form is exactly symmetric
       (p00 - k_soc*p01 == p01 - k_bias*p00, since both equal p00*p01/s). */
    double p00_new = p00 - k_soc * p00;
    double p01_new = p01 - k_soc * p01;
    double p11_new = p11 - k_bias * p01;

    if (soc_new < 0.0) soc_new = 0.0;
    if (soc_new > 100.0) soc_new = 100.0;

    e->soc_percent = soc_new;
    e->bias_ca = bias_new;
    e->p00 = p00_new;
    e->p01 = p01_new;
    e->p11 = p11_new;
}

uint8_t soc_estimator_get_percent_x2(const soc_estimator_t *e) {
    double percent_x2 = e->soc_percent * 2.0;
    if (percent_x2 < 0.0) percent_x2 = 0.0;
    if (percent_x2 > 200.0) percent_x2 = 200.0;
    return (uint8_t)(percent_x2 + 0.5); /* round to the nearest 0.5% tick */
}

int32_t soc_estimator_get_variance_x1000(const soc_estimator_t *e) {
    double v = e->p00 * 1000.0;
    if (v < 0.0) v = 0.0; /* covariance is mathematically non-negative; guard fp round-off */
    return (int32_t)(v + 0.5);
}

int32_t soc_estimator_get_bias_ca_x10(const soc_estimator_t *e) {
    double v = e->bias_ca * 10.0;
    return (v >= 0.0) ? (int32_t)(v + 0.5) : -(int32_t)(-v + 0.5);
}
