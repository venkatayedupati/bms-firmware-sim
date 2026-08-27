/*
 * Prints the real wire bytes can_protocol.c's pack_* functions produce for
 * a handful of fixed, known input values, one message per line as
 * "<id-hex> <dlc> <data-hex>". verify_dbc.py decodes each line against
 * dbc/bms.dbc (via cantools) and checks the result against the same known
 * inputs, so bms.dbc's signal layout is checked against this project's
 * actual pack/unpack code, not just hand-derived DBC bit-position math.
 *
 * Not built by the default `make sim`/`make test` -- only `make dbc-verify`
 * builds and runs this.
 */
#include <stdio.h>
#include "can_protocol.h"

static void print_frame(const can_frame_t *f) {
    printf("%03X %d ", f->id, f->dlc);
    for (int i = 0; i < f->dlc; i++) printf("%02X", f->data[i]);
    printf("\n");
}

int main(void) {
    bms_status_t status = {
        .pack_voltage_mv = 18465,
        .pack_current_ca = -200,
        .soc_percent_x2 = 196,
        .state = BMS_STATE_SHUTDOWN,
    };
    can_frame_t f_status;
    can_protocol_pack_status(&status, &f_status);
    print_frame(&f_status);

    cell_voltages_t cv = { .cell_mv = { 3650, 3700, 3695, 3712, 3680 } };
    can_frame_t f_cv;
    can_protocol_pack_cell_voltages(&cv, &f_cv);
    print_frame(&f_cv);

    fault_report_t fr = { .fault_bitmask = 0x0009, .severity = 4 };
    can_frame_t f_fault;
    can_protocol_pack_fault(&fr, &f_fault);
    print_frame(&f_fault);

    return 0;
}
