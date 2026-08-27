#include "test.h"
#include "../src/can/can_protocol.h"
#include "../src/bms/fault_manager.h" /* bms_asil_t (BMS_ASIL_D etc.) */
#include <string.h>

static void test_status_roundtrip(void) {
    bms_status_t in = { .pack_voltage_mv = 14800, .pack_current_ca = -350,
                         .soc_percent_x2 = 164, .state = BMS_STATE_WARNING };
    can_frame_t frame;
    can_protocol_pack_status(&in, &frame);
    TEST_ASSERT_EQ_INT(CAN_ID_BMS_STATUS, frame.id, "status frame uses correct CAN ID");

    bms_status_t out;
    int rc = can_protocol_unpack_status(&frame, &out);
    TEST_ASSERT_EQ_INT(0, rc, "status unpack succeeds");
    TEST_ASSERT_EQ_INT(in.pack_voltage_mv, out.pack_voltage_mv, "pack_voltage_mv round-trips");
    TEST_ASSERT_EQ_INT(in.pack_current_ca, out.pack_current_ca, "pack_current_ca round-trips (signed)");
    TEST_ASSERT_EQ_INT(in.soc_percent_x2, out.soc_percent_x2, "soc_percent_x2 round-trips");
    TEST_ASSERT_EQ_INT(in.state, out.state, "state round-trips");
}

static void test_cell_voltages_roundtrip(void) {
    /* BMS_CELL_COUNT is 5 (see bms_config.h): this frame is 10 bytes,
       genuinely over classic CAN's 8-byte limit -- this round-trip test
       is itself confirmation that CAN_MAX_DLC's CAN-FD-sized payload
       (see can_hal.h) is actually exercised, not just declared. */
    cell_voltages_t in = { .cell_mv = {3650, 3700, 3695, 3712, 3680} };
    can_frame_t frame;
    can_protocol_pack_cell_voltages(&in, &frame);
    TEST_ASSERT_EQ_INT(BMS_CELL_COUNT * 2, frame.dlc,
                        "cell voltages frame is sized for BMS_CELL_COUNT cells, not a fixed 8 bytes");

    cell_voltages_t out;
    int rc = can_protocol_unpack_cell_voltages(&frame, &out);
    TEST_ASSERT_EQ_INT(0, rc, "cell voltages unpack succeeds");
    for (int i = 0; i < BMS_CELL_COUNT; i++) {
        TEST_ASSERT_EQ_INT(in.cell_mv[i], out.cell_mv[i], "cell voltage round-trips");
    }
}

static void test_fault_report_roundtrip(void) {
    fault_report_t in = { .fault_bitmask = FAULT_BIT_OVERVOLTAGE | FAULT_BIT_OVERTEMP,
                           .severity = BMS_ASIL_D };
    can_frame_t frame;
    can_protocol_pack_fault(&in, &frame);

    fault_report_t out;
    int rc = can_protocol_unpack_fault(&frame, &out);
    TEST_ASSERT_EQ_INT(0, rc, "fault report unpack succeeds");
    TEST_ASSERT_EQ_INT(in.fault_bitmask, out.fault_bitmask, "fault bitmask round-trips");
    TEST_ASSERT_EQ_INT(in.severity, out.severity, "severity round-trips");
}

static void test_unpack_rejects_wrong_id(void) {
    can_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.id = CAN_ID_CHARGE_COMMAND; /* wrong id for a status frame */
    frame.dlc = 6;

    bms_status_t out;
    int rc = can_protocol_unpack_status(&frame, &out);
    TEST_ASSERT_EQ_INT(-1, rc, "unpack rejects a frame with the wrong CAN ID");
}

static void test_unpack_rejects_short_dlc(void) {
    can_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.id = CAN_ID_CELL_VOLTAGES;
    /* 8 bytes is classic CAN's own max DLC -- using it here (rather than
       an arbitrary small number) shows this isn't just "too short", it's
       specifically not enough anymore now that BMS_CELL_COUNT is 5
       (needs BMS_CELL_COUNT*2=10 bytes). */
    frame.dlc = 8;

    cell_voltages_t out;
    int rc = can_protocol_unpack_cell_voltages(&frame, &out);
    TEST_ASSERT_EQ_INT(-1, rc, "unpack rejects a frame that is too short for BMS_CELL_COUNT cells");
}

static void test_charge_command_unpack(void) {
    can_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.id = CAN_ID_CHARGE_COMMAND;
    frame.dlc = 2;
    frame.data[0] = 0x00;
    frame.data[1] = 0x64; /* 100 -> 10.0A in 0.1A units */

    charge_command_t out;
    int rc = can_protocol_unpack_charge_command(&frame, &out);
    TEST_ASSERT_EQ_INT(0, rc, "charge command unpack succeeds");
    TEST_ASSERT_EQ_INT(100, out.requested_current_x10, "charge command value decodes correctly");
}

void test_can_protocol_suite(void) {
    test_status_roundtrip();
    test_cell_voltages_roundtrip();
    test_fault_report_roundtrip();
    test_unpack_rejects_wrong_id();
    test_unpack_rejects_short_dlc();
    test_charge_command_unpack();
}
