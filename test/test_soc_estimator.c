#include "test.h"
#include "../src/bms/soc_estimator.h"

void test_soc_estimator_suite(void) {
    soc_estimator_t e;

    soc_estimator_init(&e, 5000);
    TEST_ASSERT_EQ_INT(200, soc_estimator_get_percent_x2(&e), "starts at 100%");
    TEST_ASSERT_EQ_INT(1000, soc_estimator_get_variance_x1000(&e),
                        "starts at the configured initial variance");

    /* Discharge at 2A (200 centi-amps) for a full simulated hour: 2A * 1h =
       2000mAh consumed out of 5000mAh -> the Coulomb-counting prediction is
       exactly 60%. Passing a voltage that the OCV table also reads as
       exactly 60% (3800mV) makes the measurement agree perfectly with the
       prediction -- so however much the Kalman gain trusts the measurement,
       the residual is exactly zero and the fused result must equal both
       inputs exactly, regardless of gain. This is the property that keeps
       an exact-percentage assertion meaningful for a filter that's
       otherwise fusing two noisy signals. */
    soc_estimator_update(&e, -200, 3600000, 3800);
    TEST_ASSERT_EQ_INT(120, soc_estimator_get_percent_x2(&e),
                        "60% remaining after 2A discharge for 1 hour, confirmed by OCV");

    /* Continuing to discharge past empty, with the voltage agreeing it's
       empty (3000mV), must clamp at 0% and never go negative -- regardless
       of the filter, the physical SoC can't be negative. */
    soc_estimator_update(&e, -200, 3600000 * 5, 3000);
    TEST_ASSERT_EQ_INT(0, soc_estimator_get_percent_x2(&e), "clamps at 0% when fully drained");

    /* Charging back up past full, with the voltage agreeing it's full
       (4200mV), must clamp at 100% and never overshoot. */
    soc_estimator_t e2;
    soc_estimator_init(&e2, 5000);
    soc_estimator_update(&e2, 500, 3600000 * 10, 4200);
    TEST_ASSERT_EQ_INT(200, soc_estimator_get_percent_x2(&e2), "clamps at 100% when overcharged");

    /* Idle current with a voltage the OCV table also reads as 100% (4200mV)
       -- residual is exactly zero again, so the estimate is exactly
       unchanged despite the filter actively fusing every update. */
    soc_estimator_t e3;
    soc_estimator_init(&e3, 5000);
    soc_estimator_update(&e3, 0, 60000, 4200);
    TEST_ASSERT_EQ_INT(200, soc_estimator_get_percent_x2(&e3),
                        "idle current with agreeing OCV leaves SoC exactly unchanged");
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

/*
 * The Kalman filter itself: instead of a hard on/off rest gate, every update
 * fuses the Coulomb-counting prediction with an OCV measurement whose noise
 * grows with current^2. These tests pin the filter's actual numeric output
 * (verified by hand against the update equations in soc_estimator.c) to
 * demonstrate the two ends of that behavior -- correction is fast and
 * confident at rest, and negligible under heavy load -- without a fixed
 * time threshold anywhere.
 */
void test_soc_estimator_kalman_suite(void) {
    /* --- Correction is fast at rest, and gets more confident (variance
       shrinks) with every further rest update, converging on the OCV
       reading instead of snapping to it after a fixed wait. --- */
    soc_estimator_t e;
    soc_estimator_init(&e, 5000);

    /* Drive the Coulomb count down to a confirmed 60%, same as above. */
    soc_estimator_update(&e, -200, 3600000, 3800);
    TEST_ASSERT_EQ_INT(120, soc_estimator_get_percent_x2(&e), "confirmed 60% before resting");
    TEST_ASSERT_EQ_INT(4939, soc_estimator_get_variance_x1000(&e),
                        "variance grows past the initial value from the large integrated move");

    /* Now rest (near-zero current) at a voltage the OCV table reads as
       100% (4200mV) -- a large, deliberate disagreement with the 60%
       Coulomb estimate, so the pull each step is visible. */
    soc_estimator_update(&e, 0, 5000, 4200);
    TEST_ASSERT_EQ_INT(196, soc_estimator_get_percent_x2(&e),
                        "a single 5s rest update already pulls most of the way toward OCV");
    TEST_ASSERT_EQ_INT(238, soc_estimator_get_variance_x1000(&e),
                        "variance drops sharply once a low-noise rest measurement is trusted");

    soc_estimator_update(&e, 0, 5000, 4200);
    TEST_ASSERT_EQ_INT(198, soc_estimator_get_percent_x2(&e),
                        "a second rest update converges further toward OCV");
    TEST_ASSERT_EQ_INT(122, soc_estimator_get_variance_x1000(&e),
                        "variance keeps shrinking as confidence builds");

    soc_estimator_update(&e, 0, 20000, 4200);
    TEST_ASSERT_EQ_INT(199, soc_estimator_get_percent_x2(&e),
                        "estimate keeps converging monotonically toward the OCV reading");
    TEST_ASSERT_EQ_INT(82, soc_estimator_get_variance_x1000(&e),
                        "variance continues to shrink, never oscillating");

    /* --- Under heavy sustained load, an even wildly wrong OCV reading is
       almost entirely ignored -- no hard threshold decides this, the
       current-dependent measurement noise does. --- */
    soc_estimator_t e2;
    soc_estimator_init(&e2, 5000);
    /* Coulomb counting alone predicts exactly 60%; feed a voltage the OCV
       table reads as only 50% (3700mV) -- a full 10-point disagreement,
       sustained for the entire discharge, not just one cycle. */
    soc_estimator_update(&e2, -200, 3600000, 3700);
    TEST_ASSERT_EQ_INT(120, soc_estimator_get_percent_x2(&e2),
                        "still rounds to 60% despite the OCV reading disagreeing by 10 points");
    TEST_ASSERT_EQ_INT(4939, soc_estimator_get_variance_x1000(&e2),
                        "variance is identical to the agreeing case -- disagreement biases the "
                        "estimate a little, not the filter's confidence in it");
}
