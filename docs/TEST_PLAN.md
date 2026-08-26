# Test Plan

## Philosophy

Unit tests target the three places a bug would actually be dangerous or
embarrassing in a real BMS: the SoC math, every fault state transition, and
wire-format correctness. They do **not** test the task/threading layer
directly (see "What's not unit tested" below) — that's covered by running
the simulator itself and observing correct behavior end to end, since
timing-dependent concurrent behavior is a poor fit for deterministic unit
assertions.

58 tests, all passing, run with `make test`, no external framework or
network access required (`test/test.h` is a ~30-line macro shim).

## Sanitizers

`make sanitize` (also run in CI, Linux-only — see "Sanitizers aren't the
same tool" below) builds and runs both the simulator and the test suite
under two sanitizer configurations `make test` alone doesn't cover:

- **ThreadSanitizer** (`make tsan-run`, `make tsan-test`) — flags
  unsynchronized concurrent memory access (data races). Caught a real one:
  `g_shutdown` in `src/osal/osal_posix.c` was a plain `volatile int` written
  by the main thread and read by every task thread with no synchronization.
  `volatile` blocks compiler reordering/caching, but gives *no* cross-thread
  atomicity or visibility guarantee in C — a real data race, not a
  theoretical nitpick, fixed by switching to C11 `atomic_int` with
  `atomic_load`/`atomic_store`.
- **AddressSanitizer + UndefinedBehaviorSanitizer** (`make asan-sim`,
  `make asan-test`) — flag memory-safety errors (out-of-bounds, use-after-
  free) and undefined behavior (signed overflow, misaligned access). Most
  relevant to `can_protocol.c`'s fixed-size wire-format pack/unpack and the
  fixed-point/floating-point arithmetic in `soc_estimator.c` and
  `fault_manager.c`.

**Sanitizers aren't the same tool, and they don't overlap as much as they
sound like they should.** TSan and ASan instrument memory access differently
and can't be linked into the same binary, hence the separate `build-tsan/`
and `build-asan/` trees. More importantly: TSan would **not** have caught
the `app_context_t.latest_reading` startup-ordering bug described below,
even though it's also a concurrency bug found by running the simulator
repeatedly — `latest_reading` was always read and written under the correct
mutex, so there was no unsynchronized access for TSan to flag. That bug was
a *stale-initial-value logic error*, a different bug class entirely from
what these tools detect. Sanitizers catch synchronization and memory-safety
violations; they don't catch "this shared state's default value doesn't
represent a real physical reading."

## Coverage by module

### `soc_estimator` (`test/test_soc_estimator.c`)

SoC is a 2-state Kalman filter tracking both the estimated SoC and the
current sensor's own calibration bias, fusing a Coulomb-counting prediction
(current integrated after subtracting the current bias estimate) with an
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
  numeric expectations verified against the update equations via a
  throwaway probe program (a 2-state covariance recursion isn't something
  to trust hand arithmetic for):
  - At rest, a large deliberate disagreement between the Coulomb count and
    the OCV reading is corrected quickly and *converges monotonically*
    over successive updates (checked via `soc_estimator_get_variance_x1000`
    shrinking every step) — not snapped instantly, and not gated on a fixed
    wait.
  - Under sustained heavy load, an equally large disagreement barely moves
    the estimate at all, and the filter's variance is identical to the
    agreeing case — the covariance recursion depends only on current/dt/
    capacity, never on what the measurement says, so disagreement biases
    the point estimate a little without changing the filter's confidence
    in it.
  - The filter actually learns a persistent current-sensor bias: fed a
    constant +10 centiamp sensor offset while the pack is genuinely at
    rest (voltage confirming the true, unchanging SoC every cycle), the
    bias estimate (`soc_estimator_get_bias_ca_x10`) climbs monotonically
    toward the true value over repeated cycles instead of the same drift
    being fought from scratch every time — the actual point of tracking
    bias as its own state rather than folding it into undifferentiated
    process noise.

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
  into is fully unit tested; the tasks themselves are thin wrappers. This
  is exactly how a real startup race was actually caught while adding the
  SoC Kalman filter: `app_context_t.latest_reading` started zero-initialized,
  and if the soc task's thread ran its first cycle before the sensor task
  published its first real reading, an all-zero reading looked like "at
  rest, pack is empty" — precisely the condition where the filter trusts
  the (bogus) voltage most. `make run` reproduced it in roughly 1 run out of
  ~30 before the fix (`app_context_init` now seeds `latest_reading` with a
  sane nominal/at-rest default); no unit test would have caught this, since
  it depends on real thread-scheduling nondeterminism, not the SoC math
  itself (which was, and is, correct).
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
