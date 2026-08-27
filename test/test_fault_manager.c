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

/* BMS_ASIL_C: still goes through the ordinary FAULT/debounce/3-strikes
   path below, unlike the BMS_ASIL_D faults (overvoltage, overtemp,
   watchdog) tested separately -- see "Why severity classification is
   worth doing at all" in test_severity_classification. */
static cell_reading_t make_undervoltage(void) {
    cell_reading_t r = make_nominal();
    r.cell_mv[0] = BMS_CELL_UNDERVOLTAGE_MV - 50;
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
    TEST_ASSERT_EQ_INT(BMS_ASIL_QM, fm.severity, "no active faults means BMS_ASIL_QM");
}

static void test_hard_fault_trips_fault_immediately(void) {
    fault_manager_t fm;
    fault_manager_init(&fm);
    cell_reading_t bad = make_undervoltage();

    fault_manager_evaluate(&fm, &bad, 10);
    TEST_ASSERT_EQ_INT(BMS_STATE_FAULT, fm.state, "undervoltage immediately trips FAULT");
    TEST_ASSERT(fm.active_faults & FAULT_BIT_UNDERVOLTAGE, "undervoltage bit is set");
    TEST_ASSERT_EQ_INT(BMS_ASIL_C, fm.severity, "undervoltage is BMS_ASIL_C");
}

static void test_fault_clears_after_debounce(void) {
    fault_manager_t fm;
    fault_manager_init(&fm);
    cell_reading_t bad = make_undervoltage();
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
    cell_reading_t bad = make_undervoltage();
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
       5000ms latch window -> pack latches into SHUTDOWN. This is
       BMS_ASIL_C's path to SHUTDOWN -- three strikes, unlike a single
       BMS_ASIL_D fault (see test_asil_d_fault_shuts_down_immediately). */
    fault_manager_evaluate(&fm, &bad, 1000);
    TEST_ASSERT_EQ_INT(BMS_STATE_SHUTDOWN, fm.state,
                        "third fault episode within the latch window trips SHUTDOWN");

    /* SHUTDOWN is latched: further nominal readings must not clear it. */
    fault_manager_evaluate(&fm, &ok, 2000);
    TEST_ASSERT_EQ_INT(BMS_STATE_SHUTDOWN, fm.state, "SHUTDOWN stays latched");
}

/*
 * Why severity classification is worth doing at all: a flat bitmask
 * treats a cell-imbalance blip and a thermal-runaway-risk overvoltage
 * event identically for state-machine purposes -- both just wait for
 * three strikes in five seconds. Real ISO 26262 practice doesn't give the
 * most severe hazards a grace period: a single BMS_ASIL_D fault
 * (overvoltage/overtemp: thermal runaway risk; watchdog: loss of the
 * fault monitor itself) trips SHUTDOWN on its very first occurrence,
 * bypassing FAULT_LATCH_COUNT entirely. Lower-severity faults keep the
 * ordinary three-strikes path tested above.
 */
static void test_asil_d_fault_shuts_down_immediately(void) {
    fault_manager_t fm;
    fault_manager_init(&fm);
    cell_reading_t bad = make_overvoltage();

    fault_manager_evaluate(&fm, &bad, 10);
    TEST_ASSERT_EQ_INT(BMS_STATE_SHUTDOWN, fm.state,
                        "a single overvoltage event (BMS_ASIL_D) trips SHUTDOWN immediately");
    TEST_ASSERT_EQ_INT(BMS_ASIL_D, fm.severity, "overvoltage is BMS_ASIL_D");

    /* SHUTDOWN is latched here too, same as the three-strikes path. */
    cell_reading_t ok = make_nominal();
    fault_manager_evaluate(&fm, &ok, 2000);
    TEST_ASSERT_EQ_INT(BMS_STATE_SHUTDOWN, fm.state, "SHUTDOWN stays latched");
}

static void test_watchdog_fault_shuts_down_immediately(void) {
    fault_manager_t fm;
    fault_manager_init(&fm);

    /* FAULT_BIT_TASK_WATCHDOG is BMS_ASIL_D too (losing the fault monitor
       itself is at least as severe as the worst hazard it guards
       against) -- a single report shuts down immediately, the same as
       overvoltage/overtemp, not the debounce-then-recover path a lower
       severity fault would take. */
    fault_manager_report_watchdog_fault(&fm, 0);
    TEST_ASSERT_EQ_INT(BMS_STATE_SHUTDOWN, fm.state,
                        "a single watchdog report (BMS_ASIL_D) trips SHUTDOWN immediately");
    TEST_ASSERT(fm.active_faults & FAULT_BIT_TASK_WATCHDOG, "watchdog bit is set");
    TEST_ASSERT_EQ_INT(BMS_ASIL_D, fm.severity, "watchdog fault is BMS_ASIL_D");

    cell_reading_t ok = make_nominal();
    fault_manager_evaluate(&fm, &ok, 2000);
    TEST_ASSERT_EQ_INT(BMS_STATE_SHUTDOWN, fm.state, "SHUTDOWN stays latched");
}

static void test_severity_classification(void) {
    TEST_ASSERT_EQ_INT(BMS_ASIL_QM, fault_manager_get_severity(0), "no faults is BMS_ASIL_QM");
    TEST_ASSERT_EQ_INT(BMS_ASIL_B, fault_manager_get_severity(FAULT_BIT_CELL_IMBALANCE),
                        "cell imbalance alone is BMS_ASIL_B");
    TEST_ASSERT_EQ_INT(BMS_ASIL_C, fault_manager_get_severity(FAULT_BIT_UNDERVOLTAGE),
                        "undervoltage alone is BMS_ASIL_C");
    TEST_ASSERT_EQ_INT(BMS_ASIL_D, fault_manager_get_severity(FAULT_BIT_OVERVOLTAGE),
                        "overvoltage alone is BMS_ASIL_D");
    TEST_ASSERT_EQ_INT(BMS_ASIL_D, fault_manager_get_severity(FAULT_BIT_OVERTEMP),
                        "overtemp alone is BMS_ASIL_D");
    TEST_ASSERT_EQ_INT(BMS_ASIL_D, fault_manager_get_severity(FAULT_BIT_TASK_WATCHDOG),
                        "task watchdog alone is BMS_ASIL_D");

    /* Multiple simultaneous faults: the worst one governs, not the first
       one evaluated or some combination -- matches real ISO 26262
       practice for a system exhibiting more than one failure mode at
       once. */
    TEST_ASSERT_EQ_INT(BMS_ASIL_D,
                        fault_manager_get_severity(FAULT_BIT_CELL_IMBALANCE | FAULT_BIT_OVERVOLTAGE),
                        "worst-of-multiple: BMS_ASIL_B and BMS_ASIL_D together is BMS_ASIL_D");
    TEST_ASSERT_EQ_INT(BMS_ASIL_C,
                        fault_manager_get_severity(FAULT_BIT_UNDERVOLTAGE | FAULT_BIT_CELL_IMBALANCE),
                        "worst-of-multiple: BMS_ASIL_C and BMS_ASIL_B together is BMS_ASIL_C");
}

void test_fault_manager_suite(void) {
    test_nominal_stays_normal();
    test_hard_fault_trips_fault_immediately();
    test_fault_clears_after_debounce();
    test_repeated_faults_latch_to_shutdown();
    test_asil_d_fault_shuts_down_immediately();
    test_watchdog_fault_shuts_down_immediately();
    test_severity_classification();
}
