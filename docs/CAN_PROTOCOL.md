# CAN Protocol

DBC-equivalent message spec for this ECU node. Real projects define this in
a `.dbc` file loaded by tools like `cantools` or Vector CANoe; it's written
here as a table for readability, but the field layout, byte order, and
scale/offset conventions below are exactly what a `.dbc` would encode.

**Byte order:** big-endian for every multi-byte signal.
**Fixed point only:** no floats cross the wire — every signal is an integer
with a documented scale, matching real automotive DBC practice (a CAN frame
has 8 bytes; floats waste precision and complicate cross-vendor tooling).

## Messages

### `0x100` BMS_STATUS — TX, every 100ms

| Bytes | Signal | Type | Scale | Range | Meaning |
|-------|--------|------|-------|-------|---------|
| 0-1 | pack_voltage_mv | uint16 | 1 mV | 0-65535 | Series sum of all cell voltages |
| 2-3 | pack_current_ca | int16 | 0.01 A | ±327.67A | Signed; negative = discharging |
| 4 | soc_percent_x2 | uint8 | 0.5% | 0-100% | State of charge |
| 5 | state | uint8 | enum | 0-3 | `bms_state_t`: 0=NORMAL, 1=WARNING, 2=FAULT, 3=SHUTDOWN |

DLC: 6.

### `0x101` CELL_VOLTAGES — TX, every 100ms

| Bytes | Signal | Type | Scale | Meaning |
|-------|--------|------|-------|---------|
| 0-1 | cell_1_mv | uint16 | 1 mV | Cell 1 voltage |
| 2-3 | cell_2_mv | uint16 | 1 mV | Cell 2 voltage |
| 4-5 | cell_3_mv | uint16 | 1 mV | Cell 3 voltage |
| 6-7 | cell_4_mv | uint16 | 1 mV | Cell 4 voltage |

DLC: 8. (A real pack with more cells would need multiplexed frames or
CAN-FD; this demo hardcodes a 4S pack to keep the wire format simple — see
`docs/ARCHITECTURE.md` "Future work".)

### `0x080` FAULT_REPORT — TX, on change only

| Bytes | Signal | Type | Meaning |
|-------|--------|------|---------|
| 0-1 | fault_bitmask | uint16 | Bitmask, see below |

DLC: 2. Sent only when the bitmask changes, not on a fixed period — a fault
report is an event, not telemetry, and flooding the bus with an unchanged
value would waste bandwidth other ECUs need.

`fault_bitmask` bits (`src/can/can_protocol.h`):

| Bit | Name | Set when |
|-----|------|----------|
| 0 | OVERVOLTAGE | Any cell ≥ 4200 mV |
| 1 | UNDERVOLTAGE | Any cell ≤ 3000 mV |
| 2 | OVERTEMP | Any cell ≥ 60°C |
| 3 | CELL_IMBALANCE | max(cell) − min(cell) ≥ 150 mV |
| 4 | TASK_WATCHDOG | A task heartbeat went stale (firmware-internal fault, not a cell condition) |

### `0x200` CHARGE_COMMAND — RX

| Bytes | Signal | Type | Scale | Meaning |
|-------|--------|------|-------|---------|
| 0-1 | requested_current_x10 | uint16 | 0.1 A | Charger's requested current |

DLC: 2. This node only logs the command in the current build (see
`src/tasks/task_can.c:on_charge_command`); a v2 would clamp/veto it against
the fault state before acting on it.

## Message IDs at a glance

| ID | Name | Direction | Period |
|----|------|-----------|--------|
| `0x080` | FAULT_REPORT | TX | on change |
| `0x100` | BMS_STATUS | TX | 100ms |
| `0x101` | CELL_VOLTAGES | TX | 100ms |
| `0x200` | CHARGE_COMMAND | RX | as sent |

IDs are 11-bit standard identifiers. FAULT_REPORT is deliberately given the
lowest ID so it wins lowest-ID-wins CAN arbitration over routine telemetry
if both are ever pending at the same instant — a safety-relevant event
should never lose a bus race to a periodic status frame. Routine telemetry
(STATUS/CELL_VOLTAGES) and the inbound charge command use higher IDs since
neither is time-critical relative to a fault.
