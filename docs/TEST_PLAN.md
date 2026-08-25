# Test Plan

## Philosophy

Unit tests target the three places a bug would actually be dangerous or
embarrassing in a real BMS: the SoC math, every fault state transition, and
wire-format correctness. They do **not** test the task/threading layer
directly (see "What's not unit tested" below) — that's covered by running
the simulator itself and observing correct behavior end to end, since
timing-dependent concurrent behavior is a poor fit for deterministic unit
assertions.

46 tests, all passing, run with `make test`, no external framework or
network access required (`test/test.h` is a ~30-line macro shim).

## Coverage by module

### `soc_estimator` (`test/test_soc_estimator.c`)

- Starts at 100%.
- A known discharge profile (2A for 1 simulated hour against a 5000mAh
  pack) produces the exact expected 60% — this is the one place a unit
  test asserts a precise numeric outcome rather than just a state
  transition, because the whole point of Coulomb counting is that the
  arithmetic has to be exactly right.
- Clamps at 0% when over-discharged (never goes negative).
- Clamps at 100% when over-charged (never overflows past full).
- Zero current for any duration leaves the Coulomb-counted value itself
  unchanged (separate from OCV correction, below).
- The OCV lookup table (`soc_estimator_ocv_lookup_percent_x2`) is tested
  directly: exact breakpoints, linear interpolation between them, and
  clamping outside the table's voltage range.
- Rest-triggered OCV correction: a deliberately drifted Coulomb count does
  **not** correct on a rest shorter than `BMS_OCV_REST_MS`, corrects exactly
  to the OCV-derived value once that full rest window elapses, and never
  corrects at all while pack current stays outside the rest band — loaded
  voltage isn't a valid OCV sample regardless of how long it's sustained.

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
