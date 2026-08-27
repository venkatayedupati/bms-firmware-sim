#!/usr/bin/env python3
"""
Reads verify_dbc.c's stdout (one "<id-hex> <dlc> <data-hex>" line per
message) and confirms dbc/bms.dbc decodes each real wire frame back to the
same known input values verify_dbc.c packed them from. Checks bms.dbc
against this project's actual pack code, not just hand-derived DBC bit
positions -- run via `make dbc-verify`, not directly.
"""
import sys
from pathlib import Path

import cantools

EXPECTED = {
    0x100: {
        "pack_voltage_mv": 18465,
        "pack_current_ca": -2.0,
        "soc_percent_x2": 98.0,
        "state": "SHUTDOWN",
    },
    0x101: {
        "cell_1_mv": 3650,
        "cell_2_mv": 3700,
        "cell_3_mv": 3695,
        "cell_4_mv": 3712,
        "cell_5_mv": 3680,
    },
    0x080: {
        "fault_bitmask": 9,
        "severity": "ASIL_D",
    },
}


def main() -> int:
    dbc_path = Path(__file__).parent / "bms.dbc"
    db = cantools.database.load_file(dbc_path)

    lines = [line for line in sys.stdin.read().splitlines() if line.strip()]
    if len(lines) != len(EXPECTED):
        print(f"expected {len(EXPECTED)} frames from verify_dbc, got {len(lines)}")
        return 1

    ok = True
    for line in lines:
        id_hex, dlc_str, data_hex = line.split()
        can_id = int(id_hex, 16)
        data = bytes.fromhex(data_hex)

        if can_id not in EXPECTED:
            print(f"0x{can_id:03X}: no expected values recorded in this script")
            ok = False
            continue

        msg = db.get_message_by_frame_id(can_id)
        if len(data) != int(dlc_str):
            print(f"0x{can_id:03X}: dlc mismatch between verify_dbc.c's stdout and itself: "
                  f"{dlc_str} vs {len(data)}")
            ok = False

        decoded = msg.decode(data)
        expected = EXPECTED[can_id]
        if decoded != expected:
            print(f"0x{can_id:03X} ({msg.name}): mismatch\n  dbc decoded: {decoded}\n  expected:    {expected}")
            ok = False
        else:
            print(f"0x{can_id:03X} ({msg.name}): OK -- {decoded}")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
