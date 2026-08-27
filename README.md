# BMS Firmware Simulator

A simulated automotive **Battery Management System (BMS)** ECU node: it
monitors a 5-cell pack, estimates State of Charge with a Kalman filter,
runs a fault-detection state machine, and reports over a CAN bus —
architected as real vehicle firmware would be, but runs entirely on a
laptop with **no hardware, no kernel modules, and no external
dependencies**.

Built to demonstrate the specific skill set automotive firmware roles screen
for: C, RTOS-style concurrency, CAN protocol design, real-time fault
handling, and hardware/software boundary design — not just "can write C."

## Why this project

Five things distinguish it from a typical embedded portfolio project:

1. **It's a real control problem, not a blinking LED.** SoC isn't plain
   Coulomb counting — it's a 2-state Kalman filter tracking both SoC and
   the current sensor's own calibration bias, fusing integrated current
   with an OCV-derived voltage measurement every update. A
   debounced/latching fault state machine and a watchdog round out the
   parts of a real BMS a review board will actually ask about. See
   ["Why coulomb counting instead of something fancier?"](docs/INTERVIEW_NOTES.md)
   for the design rationale.
2. **It's built to be ported, and both ports are real code, not a plan —
   one of them fully proven.** The OS calls (`osal/`) and CAN transport
   (`can/can_hal.h`) are both behind clean interfaces. The default host
   build implements them with pthreads and an in-process virtual bus.
   `osal_freertos.c` swaps in a real FreeRTOS kernel (vendored as a git
   submodule): `make freertos-run` cross-compiles this project's actual
   task/domain code and boots it under QEMU's `mps2-an385` (Cortex-M3)
   emulation, where it produces the exact same fault-detection output, to
   the millisecond, as the host build — genuinely verified, not just
   compiled. `can_hal_socketcan.c` binds to a real Linux kernel `vcan0`/
   `can0` interface instead of the virtual bus (`make sim-socketcan`);
   CI verifies it compiles and links cleanly against real Linux CAN
   headers, but proving it sends/receives real frames needs a genuine
   `vcan0`, which — verified directly — neither Docker Desktop nor
   GitHub's hosted Linux runner can provide. See
   [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#portability) for exactly
   what was checked on each.
3. **It's tested, not just demoed.** 74 unit tests cover the SoC math, every
   fault-state transition (including the fault-latch-to-SHUTDOWN path), and
   CAN frame packing/unpacking — see [`docs/TEST_PLAN.md`](docs/TEST_PLAN.md).
4. **It's checked for the bug class that actually matters here.** Five
   concurrent tasks share state through one mutex, so CI also runs the
   simulator and test suite under ThreadSanitizer, AddressSanitizer, and
   UBSan (`make sanitize`) — not just for show: it caught a real, silent
   data race in the OSAL shutdown flag (`src/osal/osal_posix.c`), fixed with
   C11 atomics. See ["Sanitizers"](docs/TEST_PLAN.md#sanitizers).
5. **It's linted for real, not just formatted.** CI also runs clang-tidy and
   cppcheck (`make tidy`, `make cppcheck-run`) and measures test coverage
   with lcov (`make coverage`) — every suppressed check in `.clang-tidy` has
   a one-line reason (a false positive on this platform, or a check that
   fights a deliberate convention), not a blanket disable.
6. **The one place that parses untrusted bytes is fuzzed, not just unit
   tested.** `can_protocol.c`'s `unpack_*` functions are what a real
   `CAN_RAW` socket hands attacker/peer-controlled `id`/`dlc`/`data` to;
   `make fuzz-run` runs a libFuzzer harness against them under
   ASan/UBSan (`fuzz/fuzz_can_protocol.c`, also in CI). Zero crashes across
   tens of millions of runs — see
   [`docs/TEST_PLAN.md`](docs/TEST_PLAN.md#fuzzing) for why that's the
   expected result given the code, not fuzzing doing nothing.

## Architecture at a glance

```
┌─────────────────────────────────────────────────────────────┐
│                         main.c                               │
│           (wires 5 tasks together, owns sim lifecycle)       │
└─────────────────────────────────────────────────────────────┘
        │            │            │            │            │
        ▼            ▼            ▼            ▼            ▼
   ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌───────────┐
   │ sensor  │  │  soc    │  │  fault  │  │   can   │  │ watchdog  │
   │ 100ms   │  │ 500ms   │  │  50ms   │  │  100ms  │  │  200ms    │
   └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘  └─────┬─────┘
        │            │            │            │             │
        └────────────┴─────┬──────┴────────────┴─────────────┘
                            ▼
                 app_context_t (mutex-guarded shared state)
                            │
          ┌─────────────────┴─────────────────┐
          ▼                                    ▼
   bms/ (domain logic)                 can/ (protocol + transport)
   cell_model, soc_estimator,          can_protocol (DBC-style pack/unpack)
   fault_manager                       can_hal (virtual bus / real transport)
          │                                    │
          ▼                                    ▼
   osal/ (OS abstraction: tasks, queues, mutexes, tick)
   → osal_posix.c (pthreads, used by the default host build)
   → osal_freertos.c (real FreeRTOS kernel, boots under QEMU -- see below)
```

Full design rationale, the fault state machine diagram, and the portability
story are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). The CAN message
layout (DBC-equivalent) is in [`docs/CAN_PROTOCOL.md`](docs/CAN_PROTOCOL.md).

## Build & run

The default host build requires only a C11 compiler and pthreads (works out
of the box on macOS and Linux; no cmake, no package manager, no
submodules).

```sh
make test         # build and run all 74 unit tests
make run          # build and run the simulator, nominal scenario, 8s
make run SCENARIO=overvoltage   # inject a fault scenario
make sanitize      # build + run the sim and tests under TSan, ASan, and UBSan
make tidy          # clang-tidy (requires LLVM; see CLANG_TIDY in the Makefile)
make cppcheck-run  # cppcheck
make coverage      # build + run tests with coverage instrumentation, print report
make fuzz-run      # fuzz can_protocol.c's unpack functions under ASan/UBSan (needs a libFuzzer-capable clang; see docs/TEST_PLAN.md#fuzzing)
make sim-socketcan # Linux only: build against real vcan0/can0 instead of the virtual bus
make run-socketcan # ...and run it (needs a real vcan0/can0 already set up)
```

The FreeRTOS port needs the vendored kernel submodule, the real ARM GNU
Toolchain (`brew install --cask gcc-arm-embedded`; Homebrew's plain
`arm-none-eabi-gcc` formula is header-less and can't build this — see
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#portability)), and
`qemu-system-arm`:

```sh
git submodule update --init
make freertos-build   # cross-compile for QEMU's mps2-an385 (Cortex-M3)
make freertos-run     # ...and boot it under QEMU (Ctrl-A X to quit)
```

Available scenarios: `nominal`, `overvoltage`, `undervoltage`, `overtemp`,
`imbalance`.

### What you'll see

Each task logs with a millisecond timestamp and its name. In a fault
scenario, expect something like:

```
[     0 ms] INFO  sensor   task started (period=100ms)
[     0 ms] WARN  fault    state change -> 3 (active_faults=0x0009)
...
=== Final status ===
pack_voltage_mv=19022 pack_current_ca=-200 soc=98.5% state=3 active_faults=0x0009
```

`state=3` is `BMS_STATE_SHUTDOWN`; `active_faults` is a bitmask (see
`src/can/can_protocol.h`) — `0x0009` here decodes to overvoltage (bit 0) plus
the cell imbalance that overvoltage on one cell necessarily causes (bit 3).
Overvoltage is `BMS_ASIL_D` (see `docs/ARCHITECTURE.md` "Fault state
machine"), so this scenario now latches straight to SHUTDOWN on the very
first evaluation instead of passing through `BMS_STATE_FAULT` first — a
lower-severity fault (e.g. cell imbalance alone) still would.

## Project layout

```
src/
  osal/     OS abstraction layer (tasks, queues, mutexes) — pthread + real FreeRTOS backends
  can/      CAN HAL (virtual bus + real SocketCAN backend) + protocol pack/unpack
  bms/      Domain logic: cell model, SoC Kalman filter, fault state machine
  tasks/    The 5 periodic tasks + their shared app_context_t
  main.c    Wiring and sim lifecycle
targets/
  qemu_mps2_an385/  Board support (linker script, startup, UART) for the FreeRTOS/QEMU port
third_party/
  FreeRTOS-Kernel/  Vendored FreeRTOS kernel (git submodule)
test/       74 dependency-free unit tests (no external test framework)
fuzz/       libFuzzer harness for can_protocol.c's unpack functions
docs/       Architecture, CAN protocol spec, test plan
```

## Roadmap / what a v2 would add

- Real ADC-driven cell sensing on physical STM32/ESP32 hardware — the
  FreeRTOS port to QEMU is done and verified, CAN-FD and ISO 26262-style
  fault severity are done too; this is specifically the "on real silicon,
  with a real AFE" step beyond all of that, which needs physical hardware
  this project doesn't have access to.
