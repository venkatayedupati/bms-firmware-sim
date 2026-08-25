# Test Plan

## Philosophy

Unit tests target the three places a bug would actually be dangerous or
embarrassing in a real BMS: the SoC math, every fault state transition, and
wire-format correctness. They do **not** test the task/threading layer
directly (see "What's not unit tested" below) — that's covered by running
the simulator itself and observing correct behavior end to end, since
timing-dependent concurrent behavior is a poor fit for deterministic unit
assertions.

53 tests, all passing, run with `make test`, no external framework or
network access required (`test/test.h` is a ~30-line macro shim).

## Coverage by module

### `soc_estimator` (`test/test_soc_estimator.c`)

SoC is a scalar Kalman filter fusing a Coulomb-counting prediction with an
OCV-lookup measurement every update (see `src/bms/soc_estimator.c`). Most
tests below feed a voltage the OCV table reads as *exactly* the same
percentage the Coulomb count predicts — making the residual exactly zero,
so the fused result must equal both inputs exactly regardless of the
Kalman gain. That's what keeps precise integer assertions meaningful for a
filter whose whole point is blending two noisy signals.

- Starts at 100%, with the configured initial variance.
- A known discharge profile (2A for 1 simulated hour against a 5000mAh
  pack), with agreeing OCV, produces the exact expected 60% — the
  zero-residual property above, not a coincidence.
- Clamps at 0%/100% when massively over-discharged/over-charged, even with
  agreeing OCV, since the physical SoC can't leave that range regardless of
  what the filter computes internally.
- Idle current with agreeing OCV leaves SoC exactly unchanged.
- The OCV lookup table (`soc_estimator_ocv_lookup_percent_x2`) is tested
  directly: exact breakpoints, linear interpolation between them, and
  clamping outside the table's voltage range.
- The filter's actual behavior (`test_soc_estimator_kalman_suite`), with
  numeric expectations verified by hand against the update equations:
  - At rest, a large deliberate disagreement between the Coulomb count and
    the OCV reading is corrected quickly and *converges monotonically*
    over successive updates (checked via `soc_estimator_get_variance_x1000`
    shrinking every step) — not snapped instantly, and not gated on a fixed
    wait.
  - Under sustained heavy load, an equally large disagreement barely moves
    the estimate at all, and the filter's variance is identical to the
    agreeing case — disagreement biases the point estimate a little, not
    the filter's confidence in it, since measurement noise (not a
    threshold) is what's suppressing trust in the voltage reading.

### `fault_manager` (`test/test_fault_manager.c`)

Walks the full state diagram in `docs/ARCHITECTURE.md`:

- Nominal readings stay in NORMAL.
- A hard limit violation (overvoltage) trips FAULT on the very next
  evaluation — no debounce going in.
- A single OK reading does **not** clear FAULT (debounce required); a full
  `FAULT_DEBOUNCE_MS` of continuous OK readings does.
- Three separate FAULT episodes inside `FAULT_LATCH_WINDOW_MS` latch the
  state machine into SHUTDOWN — this is the test most worth reading, since
  it's the subtlest behavior in the codebase (timestamps are chosen by hand
  to land exactly on both sides of the debounce and latch-window
  boundaries).
- SHUTDOWN, once entered, does not clear on subsequent OK readings (latched
  by design).
- A watchdog-reported fault behaves like a hard fault for state-machine
  purposes (trips FAULT, respects its own debounce window before allowing
  recovery).

### `can_protocol` (`test/test_can_protocol.c`)

- Round-trip pack→unpack for every message type (STATUS, CELL_VOLTAGES,
  FAULT_REPORT, CHARGE_COMMAND), checking every field survives the wire
  encoding — including a signed field (`pack_current_ca`) to catch sign-
  extension bugs specifically.
- Unpack rejects a frame with the wrong CAN ID.
- Unpack rejects a frame with too short a DLC rather than reading past the
  end of `data[]`.

## What's not unit tested (and why)

- **Task scheduling/timing** — exercised by running `make run` and reading
  the log output, not by assertions, because asserting on wall-clock
  interleaving of five pthreads is either flaky or trivial depending on how
  loosely you write it. The state-machine and SoC logic those tasks call
  into is fully unit tested; the tasks themselves are thin wrappers.
- **The virtual CAN bus's arbitration behavior** — `can_hal_virtual.c`
  documents in its header comment exactly what it does and doesn't model
  (broadcast delivery, not real contention/backoff). Testing it against
  real arbitration semantics would require a real bus or a much heavier
  simulation, out of scope for what this HAL is standing in for.
- **The cell model's simulated waveforms** — it's a test fixture, not
  production logic; asserting on its exact drift curve would be testing the
  test data itself.

## If this were headed for real hardware

Add: hardware-in-the-loop tests against real AFE chip communication,
CAN bus load/timing tests on real silicon (arbitration, bus-off recovery),
and static analysis / MISRA-C compliance checking, none of which are
meaningful to simulate on a host build.
