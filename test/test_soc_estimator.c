#include "test.h"
#include "../src/bms/soc_estimator.h"

void test_soc_estimator_suite(void) {
    soc_estimator_t e;

    soc_estimator_init(&e, 5000);
    TEST_ASSERT_EQ_INT(200, soc_estimator_get_percent_x2(&e), "starts at 100%");

    /* Discharge at 2A (200 centi-amps) for a full simulated hour in one
       update: 2A * 1h = 2000mAh consumed out of 5000mAh -> 60% remaining.
       avg_cell_mv is irrelevant here since current is well outside the rest
       band and can't trigger OCV correction. */
    soc_estimator_update(&e, -200, 3600000, 3700);
    TEST_ASSERT_EQ_INT(120, soc_estimator_get_percent_x2(&e),
                        "60% remaining after 2A discharge for 1 hour");

    /* Continuing to discharge past empty must clamp at 0%, never go negative. */
    soc_estimator_update(&e, -200, 3600000 * 5, 3700);
    TEST_ASSERT_EQ_INT(0, soc_estimator_get_percent_x2(&e), "clamps at 0% when fully drained");

    /* Charging back up must clamp at 100%, never overshoot. */
    soc_estimator_update(&e, 500, 3600000 * 10, 3700);
    TEST_ASSERT_EQ_INT(200, soc_estimator_get_percent_x2(&e), "clamps at 100% when overcharged");

    /* Zero current for any duration must not move SoC (the coulomb-counted
       value itself never moves at zero current; passing the OCV that
       matches full is what keeps the eventual rest-triggered correction,
       once it fires, a no-op here too). */
    soc_estimator_t e2;
    soc_estimator_init(&e2, 5000);
    soc_estimator_update(&e2, 0, 60000, 4200);
    TEST_ASSERT_EQ_INT(200, soc_estimator_get_percent_x2(&e2), "idle current does not change SoC");
}

void test_soc_estimator_ocv_lookup_suite(void) {
    TEST_ASSERT_EQ_INT(200, soc_estimator_ocv_lookup_percent_x2(4200), "table max is 100%");
    TEST_ASSERT_EQ_INT(0, soc_estimator_ocv_lookup_percent_x2(3000), "table min is 0%");
    TEST_ASSERT_EQ_INT(200, soc_estimator_ocv_lookup_percent_x2(5000), "clamps above table max");
    TEST_ASSERT_EQ_INT(0, soc_estimator_ocv_lookup_percent_x2(2500), "clamps below table min");
    /* 3650mV is exactly halfway between the (3600, 70) and (3700, 100)
       breakpoints -> exactly halfway between their percentages too. */
    TEST_ASSERT_EQ_INT(85, soc_estimator_ocv_lookup_percent_x2(3650), "interpolates between breakpoints");
}

void test_soc_estimator_ocv_correction_suite(void) {
    soc_estimator_t e;
    soc_estimator_init(&e, 5000);

    /* Drive the Coulomb count down to 60%, same as the discharge test above. */
    soc_estimator_update(&e, -200, 3600000, 3700);
    TEST_ASSERT_EQ_INT(120, soc_estimator_get_percent_x2(&e), "coulomb count at 60% before resting");

    /* Rest at a cell voltage that maps to 80% (4000mV), but for less than
       BMS_OCV_REST_MS -> too soon to trust OCV, no correction yet. */
    soc_estimator_update(&e, 0, 15000, 4000);
    TEST_ASSERT_EQ_INT(120, soc_estimator_get_percent_x2(&e), "short rest does not yet correct SoC");

    /* Crossing the full 30s rest threshold snaps the estimate to the
       OCV-derived 80%, correcting the accumulated drift. */
    soc_estimator_update(&e, 0, 15000, 4000);
    TEST_ASSERT_EQ_INT(160, soc_estimator_get_percent_x2(&e), "full rest window corrects SoC via OCV");

    /* Resuming discharge before another full rest window must not correct,
       no matter how long it runs, since loaded voltage isn't a valid OCV
       sample. */
    soc_estimator_t e2;
    soc_estimator_init(&e2, 5000);
    soc_estimator_update(&e2, -200, 3600000, 3700);
    soc_estimator_update(&e2, -200, 60000, 4200); /* still discharging; 4200mV would imply 100% if trusted */
    TEST_ASSERT_EQ_INT(118, soc_estimator_get_percent_x2(&e2),
                        "sustained discharge never triggers OCV correction");
}
