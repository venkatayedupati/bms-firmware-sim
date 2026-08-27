# Test Plan

## Philosophy

Unit tests target the three places a bug would actually be dangerous or
embarrassing in a real BMS: the SoC math, every fault state transition, and
wire-format correctness. They do **not** test the task/threading layer
directly (see "What's not unit tested" below) — that's covered by running
the simulator itself and observing correct behavior end to end, since
timing-dependent concurrent behavior is a poor fit for deterministic unit
assertions.

74 tests, all passing, run with `make test`, no external framework or
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

## Static analysis

`make tidy` (clang-tidy) and `make cppcheck-run` (cppcheck) both run in CI.
Every suppressed check in `.clang-tidy` has a one-line reason attached, not
a blanket disable — two are worth calling out specifically because they
were only discovered by running the *actual* CI tool versions, not the
locally-installed ones, echoing the same lesson as the `_POSIX_C_SOURCE`
build fix below:

- `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling`
  suggests replacing `memset`/`memcpy`/`strncpy` with C11 Annex K's `_s`
  variants. Annex K is optional and neither glibc nor macOS's libc
  implement it — the suggested fix doesn't exist on either platform this
  project targets, so the check is suppressed rather than silently ignored.
- Ubuntu's `clang-tidy` package (used in CI) is a different version from
  whatever's installed locally, and can enable checks the other doesn't —
  the check above only showed up when verified directly on Linux. Same
  root cause as `osal_posix.c` needing `_POSIX_C_SOURCE` (see
  `docs/ARCHITECTURE.md`): assuming a local dev machine's toolchain matches
  CI's is exactly the kind of assumption that's cheap to verify and
  expensive to discover from a red CI run.

Two real (if minor) findings came out of this pass rather than being
suppressed: a dead variable initializer in `osal_queue_send`/
`osal_queue_receive` (`rc`'s initial value was always overwritten before
being read), and an `int` array index in `can_protocol.c`'s cell-voltage
pack/unpack widened implicitly to a pointer offset — harmless given the
loop only ever ran 0..3 at the time, but `size_t` is the correct type for
an array index regardless. (A third finding from that same pass —
`task_can.c`'s `i < BMS_CELL_COUNT && i < 4` cell-voltage loop bound being
flagged as "condition is always true" — was suppressed rather than fixed,
since it was a deliberate guard against `cell_voltages_t`'s then-fixed
4-slot wire array overflowing once the pack grew past 4 cells. Once that
growth actually happened (`BMS_CELL_COUNT` is 5 now, `cell_voltages_t` is
`BMS_CELL_COUNT`-sized instead of fixed at 4), the guard's whole reason to
exist went away and it was deleted outright rather than left suppressed —
the doc it was pointing at eventually catching up with itself.)

## Fuzzing

`make fuzz-run` (also run in CI, `fuzz` job, 120s budget) runs a libFuzzer
harness (`fuzz/fuzz_can_protocol.c`) against every `can_protocol_unpack_*`
function — the one place in this codebase that parses bytes an attacker or
malfunctioning peer ECU actually controls. A real `CAN_RAW` socket hands
over whatever `id`/`dlc`/`data` a bus frame carried with no validation
beyond what `can_hal_socketcan.c` does; unit tests only ever construct
well-formed `can_frame_t` values, so they can't exercise the
self-inconsistent combinations (e.g. `dlc` set past `CAN_MAX_DLC`, which
`can_protocol_pack_*` never produces but nothing stops a hostile or
corrupted frame from carrying) that a fuzzer explores automatically. The
harness is built with `-fsanitize=fuzzer,address,undefined` so a
memory-safety or UB violation aborts immediately with a repro input, the
same bug classes `make sanitize` checks for, just against adversarial
input instead of the simulator's own normal execution paths.

**Result, stated honestly:** zero crashes found across tens of millions of
executions (verified directly, both locally and in a clean Ubuntu
container matching CI). This is the expected outcome given the code, not
fuzzing failing to do anything: every `unpack_*` function's loops are
bounded by compile-time constants (`BMS_CELL_COUNT`, fixed byte offsets),
never by the attacker-controlled `dlc` field itself, so there's no
variable-length parsing path for a malicious `dlc`/`data` combination to
overrun. The fuzzer earns its keep by verifying that property empirically
across the input space rather than resting on a one-time code-reading
argument — and it's exactly the kind of check that would have caught it
immediately if a future change *did* start indexing by `dlc` instead of a
fixed bound.

libFuzzer needs clang's compiler-rt fuzzer runtime, which Apple's Command
Line Tools clang doesn't bundle (verified directly: linking
`-fsanitize=fuzzer` fails with `libclang_rt.fuzzer_osx.a not found`) but
Homebrew's LLVM and Ubuntu's plain `clang` apt package both do — same
`FUZZ_CC` override pattern as `CLANG_TIDY` above.

## Code coverage

`make coverage` (also run in CI) builds with `--coverage`, runs the test
suite, and reports line/branch/function coverage via lcov, scoped to
`src/` only (the test harness itself isn't what's being measured). The
report is direct, numeric confirmation of the coverage philosophy stated
above, not a separate claim: `soc_estimator.c` and `fault_manager.c` sit in
the high-90s%, `can_protocol.c` at 100%, and every task/OSAL/cell-model file
sits at 0% — because those are exercised by `make run`, not asserted on by
unit tests, exactly as documented in "What's not unit tested" below. There's
no enforced minimum-coverage gate: a hard threshold would either be trivially
satisfied by the already-well-tested modules or would force testing
timing-dependent task code that's a poor fit for it, so the report is there
for visibility, not as a pass/fail gate.

**Another CI-only surprise, same root cause as `_POSIX_C_SOURCE` above:**
Ubuntu 24.04's apt `lcov` package is 2.0-1, which has a real bug in
`--list`'s function-coverage percentage column -- verified by dumping the
raw `coverage.info`: the underlying `FNH`/`FNF` counts were already correct
(e.g. 6 hit / 6 found) while `--list` rendered that same data as "900%".
The CI workflow installs lcov 2.5 from its GitHub release tarball instead
(matching the Homebrew version already used in local development), which
needs its own non-core-Perl dependencies (`Capture::Tiny`, `DateTime`,
`JSON`, `PerlIO::gzip`) pulled in via apt Debian packages rather than CPAN,
since there's no network CPAN access assumed in CI. First fix attempt
guessed a gcov/gcc version mismatch instead and made no difference --
worth remembering that a plausible-sounding theory still needs to be
checked against the raw data before treating it as the actual cause.

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

Walks the full state diagram in `docs/ARCHITECTURE.md`, using a
`BMS_ASIL_C` fault (undervoltage) for every test of the *ordinary*
FAULT/debounce/3-strikes path, specifically because `BMS_ASIL_D` faults
(overvoltage, overtemp, watchdog) no longer take that path at all — see
below:

- Nominal readings stay in NORMAL (and are `BMS_ASIL_QM`: no active faults).
- A hard limit violation (undervoltage) trips FAULT on the very next
  evaluation — no debounce going in.
- A single OK reading does **not** clear FAULT (debounce required); a full
  `FAULT_DEBOUNCE_MS` of continuous OK readings does.
- Three separate undervoltage episodes inside `FAULT_LATCH_WINDOW_MS` latch
  the state machine into SHUTDOWN — this is the test most worth reading for
  the *ordinary* latch path, since it's the subtlest behavior in that path
  (timestamps are chosen by hand to land exactly on both sides of the
  debounce and latch-window boundaries).
- SHUTDOWN, once entered, does not clear on subsequent OK readings (latched
  by design), regardless of which path reached it.
- A single overvoltage event (`BMS_ASIL_D`) trips SHUTDOWN on its very
  first evaluation, skipping the 3-strikes latch window entirely — the
  actual behavioral point of classifying fault severity at all, not just a
  label. A single watchdog report (also `BMS_ASIL_D`) does the same.
- `fault_manager_get_severity()` tested directly: each fault bit's
  individual severity, and that combining multiple bits yields the *worst*
  one, not the first or a sum.

### `can_protocol` (`test/test_can_protocol.c`)

- Round-trip pack→unpack for every message type (STATUS, CELL_VOLTAGES,
  FAULT_REPORT, CHARGE_COMMAND), checking every field survives the wire
  encoding — including a signed field (`pack_current_ca`) to catch sign-
  extension bugs specifically, and (for FAULT_REPORT) the `severity` byte
  added alongside `fault_bitmask`.
- CELL_VOLTAGES packs to `BMS_CELL_COUNT * 2` bytes (10, currently) — this
  round-trip test is itself confirmation that `can_hal.h`'s CAN-FD-sized
  payload is actually exercised by this message, not just declared.
- Unpack rejects a frame with the wrong CAN ID.
- Unpack rejects a frame with too short a DLC rather than reading past the
  end of `data[]` — for CELL_VOLTAGES, specifically a `dlc` of 8 (classic
  CAN's own max), confirming that even a full classic-sized frame isn't
  enough once `BMS_CELL_COUNT` is 5.

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
