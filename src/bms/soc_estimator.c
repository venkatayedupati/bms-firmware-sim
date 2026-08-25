#include "soc_estimator.h"

void soc_estimator_init(soc_estimator_t *e, uint16_t capacity_mah) {
    e->capacity_mah = capacity_mah;
    e->remaining_mah_x100 = (int32_t)capacity_mah * 100; /* start at 100% */
}

void soc_estimator_update(soc_estimator_t *e, int16_t current_ca, uint32_t dt_ms) {
    /* delta_mAh_x100 = current_ca(0.01A) * 10(mA/0.01A) * dt_ms * 100 / 3600000
                       = current_ca * dt_ms / 3600  */
    int64_t delta = ((int64_t)current_ca * (int64_t)dt_ms) / 3600;
    e->remaining_mah_x100 += (int32_t)delta;

    int32_t cap_x100 = (int32_t)e->capacity_mah * 100;
    if (e->remaining_mah_x100 > cap_x100) e->remaining_mah_x100 = cap_x100;
    if (e->remaining_mah_x100 < 0) e->remaining_mah_x100 = 0;
}

uint8_t soc_estimator_get_percent_x2(const soc_estimator_t *e) {
    if (e->capacity_mah == 0) return 0;
    int32_t percent_x2 = (e->remaining_mah_x100 * 2) / (int32_t)e->capacity_mah;
    if (percent_x2 < 0) percent_x2 = 0;
    if (percent_x2 > 200) percent_x2 = 200;
    return (uint8_t)percent_x2;
}
