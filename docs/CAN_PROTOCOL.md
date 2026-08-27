# CAN Protocol

DBC-equivalent message spec for this ECU node, written here as a table for
readability. A real, loadable [`dbc/bms.dbc`](../dbc/bms.dbc) encodes the
exact same layout — see `make dbc-verify` (also run in CI), which checks
that `dbc/bms.dbc` decodes real wire bytes produced by this project's own
`can_protocol_pack_*` functions back to the correct values via `cantools`,
rather than trusting hand-derived DBC bit-position math on its own.

**Byte order:** big-endian for every multi-byte signal.
**Fixed point only:** no floats cross the wire — every signal is an integer
with a documented scale, matching real automotive DBC practice.
**Classic CAN and CAN-FD both:** BMS_STATUS, FAULT_REPORT, and
CHARGE_COMMAND are all 8 bytes or fewer and work on either; CELL_VOLTAGES
needs CAN-FD's larger payload once `BMS_CELL_COUNT` (currently 5) pushes it
past 8 bytes — see that message's own entry below.

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
| 8-9 | cell_5_mv | uint16 | 1 mV | Cell 5 voltage |

DLC: `BMS_CELL_COUNT * 2` = 10 (currently). This is the one message in this
protocol that's genuinely CAN-FD-only: classic CAN's 8-byte frame limit fit
a 4S pack exactly, but a 5th cell pushes it to 10 bytes. `can_hal_socketcan.c`
sends this as a real `struct canfd_frame`, not `struct can_frame`; see
`docs/ARCHITECTURE.md` "Portability". A pack larger still than CAN-FD's own
64-byte max would need multiplexed frames instead — out of scope here, since
64 bytes covers a 32-cell pack, well beyond what a 5S demo needs to prove.

### `0x080` FAULT_REPORT — TX, on change only

| Bytes | Signal | Type | Meaning |
|-------|--------|------|---------|
| 0-1 | fault_bitmask | uint16 | Bitmask, see below |
| 2 | severity | uint8 | `bms_asil_t` (see `src/bms/fault_manager.h`): worst ISO 26262-style severity among the active faults |

DLC: 3. Sent only when the bitmask changes, not on a fixed period — a fault
report is an event, not telemetry, and flooding the bus with an unchanged
value would waste bandwidth other ECUs need.

`fault_bitmask` bits (`src/can/can_protocol.h`), each with its own ISO
26262-style severity (`src/bms/fault_manager.c`'s `FAULT_SEVERITIES` table —
a real hazard analysis would derive these from severity/exposure/
controllability per failure mode, not just assign one level to "the BMS"):

| Bit | Name | Set when | Severity | Why |
|-----|------|----------|----------|-----|
| 0 | OVERVOLTAGE | Any cell ≥ 4200 mV | `BMS_ASIL_D` | Thermal runaway risk |
| 1 | UNDERVOLTAGE | Any cell ≤ 3000 mV | `BMS_ASIL_C` | Over-discharge cell damage, less acutely dangerous |
| 2 | OVERTEMP | Any cell ≥ 60°C | `BMS_ASIL_D` | Thermal runaway risk |
| 3 | CELL_IMBALANCE | max(cell) − min(cell) ≥ 150 mV | `BMS_ASIL_B` | Longevity/efficiency concern, gradual |
| 4 | TASK_WATCHDOG | A task heartbeat went stale (firmware-internal fault, not a cell condition) | `BMS_ASIL_D` | Loss of the fault monitor itself — treated at least as seriously as the worst hazard it guards against |

`severity` is the *worst* level among every bit currently set, not a sum or
the first one found — see `fault_manager_get_severity()`. A single
`BMS_ASIL_D` fault also skips the fault state machine's usual 3-strikes
latch window entirely and trips `BMS_STATE_SHUTDOWN` on its first
occurrence; see `docs/ARCHITECTURE.md` "Fault state machine".

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
