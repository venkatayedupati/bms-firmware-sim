#include "can_protocol.h"
#include <string.h>

static void put_u16(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)(v >> 8);
    buf[1] = (uint8_t)(v & 0xFF);
}

static uint16_t get_u16(const uint8_t *buf) {
    return (uint16_t)((buf[0] << 8) | buf[1]);
}

void can_protocol_pack_status(const bms_status_t *s, can_frame_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = CAN_ID_BMS_STATUS;
    out->dlc = 6;
    put_u16(&out->data[0], s->pack_voltage_mv);
    put_u16(&out->data[2], (uint16_t)s->pack_current_ca);
    out->data[4] = s->soc_percent_x2;
    out->data[5] = s->state;
}

int can_protocol_unpack_status(const can_frame_t *in, bms_status_t *out) {
    if (in->id != CAN_ID_BMS_STATUS || in->dlc < 6) return -1;
    out->pack_voltage_mv = get_u16(&in->data[0]);
    out->pack_current_ca = (int16_t)get_u16(&in->data[2]);
    out->soc_percent_x2 = in->data[4];
    out->state = in->data[5];
    return 0;
}

void can_protocol_pack_cell_voltages(const cell_voltages_t *cv, can_frame_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = CAN_ID_CELL_VOLTAGES;
    out->dlc = 8;
    for (int i = 0; i < 4; i++) {
        put_u16(&out->data[i * 2], cv->cell_mv[i]);
    }
}

int can_protocol_unpack_cell_voltages(const can_frame_t *in, cell_voltages_t *out) {
    if (in->id != CAN_ID_CELL_VOLTAGES || in->dlc < 8) return -1;
    for (int i = 0; i < 4; i++) {
        out->cell_mv[i] = get_u16(&in->data[i * 2]);
    }
    return 0;
}

void can_protocol_pack_fault(const fault_report_t *f, can_frame_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = CAN_ID_FAULT_REPORT;
    out->dlc = 2;
    put_u16(&out->data[0], f->fault_bitmask);
}

int can_protocol_unpack_fault(const can_frame_t *in, fault_report_t *out) {
    if (in->id != CAN_ID_FAULT_REPORT || in->dlc < 2) return -1;
    out->fault_bitmask = get_u16(&in->data[0]);
    return 0;
}

int can_protocol_unpack_charge_command(const can_frame_t *in, charge_command_t *out) {
    if (in->id != CAN_ID_CHARGE_COMMAND || in->dlc < 2) return -1;
    out->requested_current_x10 = get_u16(&in->data[0]);
    return 0;
}
