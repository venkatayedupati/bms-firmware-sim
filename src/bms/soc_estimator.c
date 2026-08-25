#include "soc_estimator.h"
#include "bms_config.h"
#include <stddef.h>

/* Piecewise-linear approximation of a typical Li-ion resting discharge
   curve: flatter through the middle, steeper near the empty/full ends. Only
   valid as an OCV sample when the pack has been at rest long enough for
   internal-resistance IR drop to settle out (see soc_estimator_update). */
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
    e->remaining_mah_x100 = (int32_t)capacity_mah * 100; /* start at 100% */
    e->rest_accum_ms = 0;
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
    /* delta_mAh_x100 = current_ca(0.01A) * 10(mA/0.01A) * dt_ms * 100 / 3600000
                       = current_ca * dt_ms / 3600  */
    int64_t delta = ((int64_t)current_ca * (int64_t)dt_ms) / 3600;
    e->remaining_mah_x100 += (int32_t)delta;

    int32_t cap_x100 = (int32_t)e->capacity_mah * 100;
    if (e->remaining_mah_x100 > cap_x100) e->remaining_mah_x100 = cap_x100;
    if (e->remaining_mah_x100 < 0) e->remaining_mah_x100 = 0;

    if (current_ca >= -BMS_OCV_REST_CURRENT_CA && current_ca <= BMS_OCV_REST_CURRENT_CA) {
        e->rest_accum_ms += dt_ms;
    } else {
        e->rest_accum_ms = 0;
    }

    if (e->rest_accum_ms >= BMS_OCV_REST_MS) {
        uint8_t ocv_percent_x2 = soc_estimator_ocv_lookup_percent_x2(avg_cell_mv);
        e->remaining_mah_x100 = ((int32_t)ocv_percent_x2 * (int32_t)e->capacity_mah) / 2;
    }
}

uint8_t soc_estimator_get_percent_x2(const soc_estimator_t *e) {
    if (e->capacity_mah == 0) return 0;
    int32_t percent_x2 = (e->remaining_mah_x100 * 2) / (int32_t)e->capacity_mah;
    if (percent_x2 < 0) percent_x2 = 0;
    if (percent_x2 > 200) percent_x2 = 200;
    return (uint8_t)percent_x2;
}
