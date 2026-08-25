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
    e->x_percent = 100.0; /* start at 100% */
    e->p_variance = BMS_SOC_INITIAL_VARIANCE_PCT2;
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
    /* --- Predict: Coulomb-count the current into a percent-of-capacity
       delta (same arithmetic as plain Coulomb counting, just expressed in
       percent instead of mAh: current_ca(0.01A) * dt_ms / (3600 * capacity_mah)
       is amp-hours consumed divided by capacity, as a fraction). */
    double delta_percent =
        ((double)current_ca * (double)dt_ms) / (3600.0 * (double)e->capacity_mah);
    double x_pred = e->x_percent + delta_percent;

    /* Process noise: a larger integrated move carries proportionally more
       accumulated current-sense error. A small floor keeps the filter able
       to correct a stale estimate even across updates with zero current. */
    double sense_error = BMS_SOC_CURRENT_SENSE_ERROR_FRAC * delta_percent;
    double process_noise = BMS_SOC_PROCESS_NOISE_FLOOR_PCT2 + sense_error * sense_error;
    double p_pred = e->p_variance + process_noise;

    /* --- Update: fuse against this cycle's OCV measurement. Measurement
       noise grows with current^2, since IR drop grows with current and its
       contribution to voltage error grows with the square of that. */
    double z_percent = (double)soc_estimator_ocv_lookup_percent_x2(avg_cell_mv) / 2.0;
    double current_a = (double)current_ca / 100.0;
    double measurement_noise =
        BMS_SOC_OCV_BASE_VARIANCE_PCT2 + BMS_SOC_OCV_LOAD_COEFF_PCT2_PER_A2 * current_a * current_a;

    double gain = p_pred / (p_pred + measurement_noise);
    double x_new = x_pred + gain * (z_percent - x_pred);
    double p_new = (1.0 - gain) * p_pred;

    if (x_new < 0.0) x_new = 0.0;
    if (x_new > 100.0) x_new = 100.0;

    e->x_percent = x_new;
    e->p_variance = p_new;
}

uint8_t soc_estimator_get_percent_x2(const soc_estimator_t *e) {
    double percent_x2 = e->x_percent * 2.0;
    if (percent_x2 < 0.0) percent_x2 = 0.0;
    if (percent_x2 > 200.0) percent_x2 = 200.0;
    return (uint8_t)(percent_x2 + 0.5); /* round to the nearest 0.5% tick */
}

int32_t soc_estimator_get_variance_x1000(const soc_estimator_t *e) {
    double v = e->p_variance * 1000.0;
    if (v < 0.0) v = 0.0; /* covariance is mathematically non-negative; guard fp round-off */
    return (int32_t)(v + 0.5);
}
