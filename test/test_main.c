#include "test.h"

int g_tests_run = 0;
int g_tests_failed = 0;

void test_soc_estimator_suite(void);
void test_soc_estimator_ocv_lookup_suite(void);
void test_soc_estimator_kalman_suite(void);
void test_fault_manager_suite(void);
void test_can_protocol_suite(void);

int main(void) {
    RUN_SUITE(test_soc_estimator_suite);
    RUN_SUITE(test_soc_estimator_ocv_lookup_suite);
    RUN_SUITE(test_soc_estimator_kalman_suite);
    RUN_SUITE(test_fault_manager_suite);
    RUN_SUITE(test_can_protocol_suite);

    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
