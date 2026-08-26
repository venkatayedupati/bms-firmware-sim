# BMS Firmware Simulator

A simulated automotive **Battery Management System (BMS)** ECU node: it
monitors a 4-cell pack, estimates State of Charge with a Kalman filter,
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
2. **It's built to be ported, and one port is already real code, not just a
   plan.** The OS calls (`osal/`) and CAN transport (`can/can_hal.h`) are
   both behind clean interfaces. The default host build implements them
   with pthreads and an in-process virtual bus; `can_hal_socketcan.c` is a
   second backend binding to a real Linux kernel `vcan0`/`can0` interface
   (`make sim-socketcan`), verified in CI to compile and link cleanly
   against real Linux CAN headers. Proving it sends/receives real frames
   needs a genuine `vcan0`, which — verified directly — neither Docker
   Desktop nor GitHub's hosted Linux runner can provide; see
   [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#portability) for exactly
   what was checked and how to verify it yourself on a real Linux box.
   FreeRTOS is the same story, one port away: only `osal/` would need a
   new file.
3. **It's tested, not just demoed.** 58 unit tests cover the SoC math, every
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
   → osal_posix.c (pthreads, used today)
   → osal_freertos.c (documented port target, not yet written)
```

Full design rationale, the fault state machine diagram, and the portability
story are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). The CAN message
layout (DBC-equivalent) is in [`docs/CAN_PROTOCOL.md`](docs/CAN_PROTOCOL.md).

## Build & run

Requires only a C11 compiler and pthreads (works out of the box on macOS and
Linux; no cmake, no package manager, no submodules).

```sh
make test         # build and run all 58 unit tests
make run          # build and run the simulator, nominal scenario, 8s
make run SCENARIO=overvoltage   # inject a fault scenario
make sanitize      # build + run the sim and tests under TSan, ASan, and UBSan
make tidy          # clang-tidy (requires LLVM; see CLANG_TIDY in the Makefile)
make cppcheck-run  # cppcheck
make coverage      # build + run tests with coverage instrumentation, print report
make sim-socketcan # Linux only: build against real vcan0/can0 instead of the virtual bus
make run-socketcan # ...and run it (needs a real vcan0/can0 already set up)
```

Available scenarios: `nominal`, `overvoltage`, `undervoltage`, `overtemp`,
`imbalance`.

### What you'll see

Each task logs with a millisecond timestamp and its name. In a fault
scenario, expect something like:

```
[     0 ms] INFO  sensor   task started (period=100ms)
[    52 ms] WARN  fault    state change -> 2 (active_faults=0x0009)
...
=== Final status ===
pack_voltage_mv=15200 pack_current_ca=-200 soc=99.4% state=2 active_faults=0x0009
```

`state=2` is `BMS_STATE_FAULT`; `active_faults` is a bitmask (see
`src/can/can_protocol.h`) — `0x0009` here decodes to overvoltage (bit 0) plus
the cell imbalance that overvoltage on one cell necessarily causes (bit 3).

## Project layout

```
src/
  osal/     OS abstraction layer (tasks, queues, mutexes) — pthread impl today
  can/      CAN HAL (virtual bus + real SocketCAN backend) + protocol pack/unpack
  bms/      Domain logic: cell model, SoC Kalman filter, fault state machine
  tasks/    The 5 periodic tasks + their shared app_context_t
  main.c    Wiring and sim lifecycle
test/       58 dependency-free unit tests (no external test framework)
docs/       Architecture, CAN protocol spec, test plan
```

## Roadmap / what a v2 would add

- Replace the pthread OSAL backend with real FreeRTOS (`osal_freertos.c`) and
  cross-compile for an STM32/ESP32 target with real ADC-driven cell sensing.
- CAN-FD support and ISO 26262-style fault severity classification.
