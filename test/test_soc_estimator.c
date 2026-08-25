#include "test.h"
#include "../src/bms/soc_estimator.h"

void test_soc_estimator_suite(void) {
    soc_estimator_t e;

    soc_estimator_init(&e, 5000);
    TEST_ASSERT_EQ_INT(200, soc_estimator_get_percent_x2(&e), "starts at 100%");

    /* Discharge at 2A (200 centi-amps) for a full simulated hour in one
       update: 2A * 1h = 2000mAh consumed out of 5000mAh -> 60% remaining. */
    soc_estimator_update(&e, -200, 3600000);
    TEST_ASSERT_EQ_INT(120, soc_estimator_get_percent_x2(&e),
                        "60% remaining after 2A discharge for 1 hour");

    /* Continuing to discharge past empty must clamp at 0%, never go negative. */
    soc_estimator_update(&e, -200, 3600000 * 5);
    TEST_ASSERT_EQ_INT(0, soc_estimator_get_percent_x2(&e), "clamps at 0% when fully drained");

    /* Charging back up must clamp at 100%, never overshoot. */
    soc_estimator_update(&e, 500, 3600000 * 10);
    TEST_ASSERT_EQ_INT(200, soc_estimator_get_percent_x2(&e), "clamps at 100% when overcharged");

    /* Zero current for any duration must not move SoC. */
    soc_estimator_t e2;
    soc_estimator_init(&e2, 5000);
    soc_estimator_update(&e2, 0, 60000);
    TEST_ASSERT_EQ_INT(200, soc_estimator_get_percent_x2(&e2), "idle current does not change SoC");
}
