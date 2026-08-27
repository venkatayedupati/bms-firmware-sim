#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <stdint.h>
#include "can_hal.h"
#include "../bms/bms_config.h"

/*
 * Message layout for this ECU node. Documented in full, DBC-style, in
 * docs/CAN_PROTOCOL.md. All multi-byte signals are big-endian and use a
 * fixed-point scale+offset so no floats cross the wire, matching real
 * automotive DBC convention.
 */

/* IDs are chosen so lowest-ID-wins CAN arbitration favors the
   safety-critical fault message over routine telemetry if both are ever
   pending at once. */
#define CAN_ID_FAULT_REPORT   0x080u  /* TX, on-change: active fault bitmask + ISO 26262-style severity (highest priority) */
#define CAN_ID_BMS_STATUS     0x100u  /* TX, 100ms: pack voltage/current/SoC/state */
#define CAN_ID_CELL_VOLTAGES  0x101u  /* TX, 100ms: BMS_CELL_COUNT cell voltages, mV -- CAN-FD (needs more than 8 bytes once BMS_CELL_COUNT > 4) */
#define CAN_ID_CHARGE_COMMAND 0x200u  /* RX: requested charge current, 0.1A units */

typedef enum {
    BMS_STATE_NORMAL = 0,
    BMS_STATE_WARNING = 1,
    BMS_STATE_FAULT = 2,
    BMS_STATE_SHUTDOWN = 3,
} bms_state_t;

typedef struct {
    uint16_t pack_voltage_mv;   /* 0-65535 mV */
    int16_t  pack_current_ca;   /* centi-amps, signed; negative = discharge */
    uint8_t  soc_percent_x2;    /* SoC * 2, giving 0.5% resolution in one byte */
    uint8_t  state;             /* bms_state_t */
} bms_status_t;

typedef struct {
    uint16_t cell_mv[BMS_CELL_COUNT];
} cell_voltages_t;

typedef struct {
    uint16_t fault_bitmask;
    uint8_t  severity; /* bms_asil_t (see fault_manager.h): worst ISO
                           26262-style severity among active_faults */
} fault_report_t;

typedef struct {
    uint16_t requested_current_x10; /* 0.1A units */
} charge_command_t;

/* fault_bitmask bits */
#define FAULT_BIT_OVERVOLTAGE   (1u << 0)
#define FAULT_BIT_UNDERVOLTAGE  (1u << 1)
#define FAULT_BIT_OVERTEMP      (1u << 2)
#define FAULT_BIT_CELL_IMBALANCE (1u << 3)
#define FAULT_BIT_TASK_WATCHDOG (1u << 4)

void can_protocol_pack_status(const bms_status_t *status, can_frame_t *out);
int  can_protocol_unpack_status(const can_frame_t *in, bms_status_t *out);

void can_protocol_pack_cell_voltages(const cell_voltages_t *cv, can_frame_t *out);
int  can_protocol_unpack_cell_voltages(const can_frame_t *in, cell_voltages_t *out);

void can_protocol_pack_fault(const fault_report_t *f, can_frame_t *out);
int  can_protocol_unpack_fault(const can_frame_t *in, fault_report_t *out);

int can_protocol_unpack_charge_command(const can_frame_t *in, charge_command_t *out);

#endif /* CAN_PROTOCOL_H */
