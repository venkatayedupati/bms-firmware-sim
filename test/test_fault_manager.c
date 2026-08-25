#include "test.h"
#include "../src/bms/fault_manager.h"

static cell_reading_t make_nominal(void) {
    cell_reading_t r;
    for (int i = 0; i < BMS_CELL_COUNT; i++) {
        r.cell_mv[i] = BMS_CELL_NOMINAL_MV;
        r.cell_temp_c[i] = 25;
    }
    r.pack_current_ca = -200;
    return r;
}

static cell_reading_t make_overvoltage(void) {
    cell_reading_t r = make_nominal();
    r.cell_mv[0] = BMS_CELL_OVERVOLTAGE_MV + 50;
    return r;
}

static void test_nominal_stays_normal(void) {
    fault_manager_t fm;
    fault_manager_init(&fm);
    cell_reading_t r = make_nominal();

    fault_manager_evaluate(&fm, &r, 0);
    TEST_ASSERT_EQ_INT(BMS_STATE_NORMAL, fm.state, "nominal readings stay NORMAL");
    TEST_ASSERT_EQ_INT(0, fm.active_faults, "no active faults when nominal");
}

static void test_overvoltage_trips_fault(void) {
    fault_manager_t fm;
    fault_manager_init(&fm);
    cell_reading_t bad = make_overvoltage();

    fault_manager_evaluate(&fm, &bad, 10);
    TEST_ASSERT_EQ_INT(BMS_STATE_FAULT, fm.state, "overvoltage immediately trips FAULT");
    TEST_ASSERT(fm.active_faults & FAULT_BIT_OVERVOLTAGE, "overvoltage bit is set");
}

static void test_fault_clears_after_debounce(void) {
    fault_manager_t fm;
    fault_manager_init(&fm);
    cell_reading_t bad = make_overvoltage();
    cell_reading_t ok = make_nominal();

    fault_manager_evaluate(&fm, &bad, 10);
    TEST_ASSERT_EQ_INT(BMS_STATE_FAULT, fm.state, "enters FAULT");

    /* One OK reading alone must not immediately clear -- debounce required. */
    fault_manager_evaluate(&fm, &ok, 310);
    TEST_ASSERT_EQ_INT(BMS_STATE_FAULT, fm.state, "single OK reading does not clear FAULT yet");

    /* After FAULT_DEBOUNCE_MS of continuous OK readings, it clears. */
    fault_manager_evaluate(&fm, &ok, 620);
    TEST_ASSERT_EQ_INT(BMS_STATE_NORMAL, fm.state, "clears to NORMAL after debounce window");
}

static void test_repeated_faults_latch_to_shutdown(void) {
    fault_manager_t fm;
    fault_manager_init(&fm);
    cell_reading_t bad = make_overvoltage();
    cell_reading_t ok = make_nominal();

    /* Episode 1 */
    fault_manager_evaluate(&fm, &bad, 10);
    fault_manager_evaluate(&fm, &ok, 310);
    fault_manager_evaluate(&fm, &ok, 620);
    TEST_ASSERT_EQ_INT(BMS_STATE_NORMAL, fm.state, "clears after episode 1");

    /* Episode 2 */
    fault_manager_evaluate(&fm, &bad, 650);
    fault_manager_evaluate(&fm, &ok, 660);
    fault_manager_evaluate(&fm, &ok, 970);
    TEST_ASSERT_EQ_INT(BMS_STATE_NORMAL, fm.state, "clears after episode 2");

    /* Episode 3: three FAULT entries (t=10, 650, 1000) all within the
       5000ms latch window -> pack latches into SHUTDOWN. */
    fault_manager_evaluate(&fm, &bad, 1000);
    TEST_ASSERT_EQ_INT(BMS_STATE_SHUTDOWN, fm.state,
                        "third fault episode within the latch window trips SHUTDOWN");

    /* SHUTDOWN is latched: further nominal readings must not clear it. */
    fault_manager_evaluate(&fm, &ok, 2000);
    TEST_ASSERT_EQ_INT(BMS_STATE_SHUTDOWN, fm.state, "SHUTDOWN stays latched");
}

static void test_watchdog_fault_and_recovery(void) {
    fault_manager_t fm;
    fault_manager_init(&fm);
    cell_reading_t ok = make_nominal();

    fault_manager_report_watchdog_fault(&fm, 0);
    TEST_ASSERT_EQ_INT(BMS_STATE_FAULT, fm.state, "watchdog report trips FAULT");
    TEST_ASSERT(fm.active_faults & FAULT_BIT_TASK_WATCHDOG, "watchdog bit is set");

    /* Still within the watchdog's own debounce window -> stays FAULT. */
    fault_manager_evaluate(&fm, &ok, 100);
    TEST_ASSERT_EQ_INT(BMS_STATE_FAULT, fm.state, "stays FAULT while watchdog window active");

    /* Watchdog window has elapsed and readings are OK -> starts clearing. */
    fault_manager_evaluate(&fm, &ok, 350);
    fault_manager_evaluate(&fm, &ok, 660);
    TEST_ASSERT_EQ_INT(BMS_STATE_NORMAL, fm.state, "clears once watchdog window elapses and stays OK");
}

void test_fault_manager_suite(void) {
    test_nominal_stays_normal();
    test_overvoltage_trips_fault();
    test_fault_clears_after_debounce();
    test_repeated_faults_latch_to_shutdown();
    test_watchdog_fault_and_recovery();
}
