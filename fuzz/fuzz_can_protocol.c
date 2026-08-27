/*
 * libFuzzer harness for can_protocol.c's unpack functions.
 *
 * These are the one place in this codebase that parses bytes an attacker
 * (or a malfunctioning peer ECU) actually controls: a real CAN_RAW socket
 * delivers whatever id/dlc/data a real bus frame carried, with no
 * validation beyond what can_hal_socketcan.c itself does. Every unpack_*
 * function is exercised here against a fully attacker-controlled
 * can_frame_t built directly from the fuzz input -- not just the id/dlc
 * combinations a well-behaved sender would ever produce, including
 * self-inconsistent ones (e.g. dlc set past CAN_MAX_DLC) that
 * can_protocol_pack_* never generates but nothing stops a hostile or
 * corrupted frame from carrying.
 *
 * Build/run: `make fuzz-run` (see Makefile; needs a clang with libFuzzer's
 * compiler-rt runtime -- Homebrew's LLVM on macOS, the system clang
 * package on Ubuntu; Apple's Command Line Tools clang lacks it).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "../src/can/can_protocol.h"

/* Must stay external: libFuzzer's own main() (in the compiler-rt runtime
   this links against) calls this exact symbol -- clang-tidy's suggestion
   to make it static would break the link. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { // NOLINT(misc-use-internal-linkage)
    if (size < 5) return 0;

    can_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.id = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
               ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    frame.dlc = data[4]; /* full uint8_t range, deliberately not clamped to CAN_MAX_DLC */

    size_t payload_avail = size - 5;
    size_t copy_len = payload_avail < sizeof(frame.data) ? payload_avail : sizeof(frame.data);
    memcpy(frame.data, data + 5, copy_len);

    bms_status_t status;
    can_protocol_unpack_status(&frame, &status);

    cell_voltages_t cv;
    can_protocol_unpack_cell_voltages(&frame, &cv);

    fault_report_t fr;
    can_protocol_unpack_fault(&frame, &fr);

    charge_command_t cc;
    can_protocol_unpack_charge_command(&frame, &cc);

    return 0;
}
