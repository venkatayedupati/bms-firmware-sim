# BMS Firmware Simulator

A simulated automotive **Battery Management System (BMS)** ECU node: it
monitors a 4-cell pack, estimates State of Charge, runs a fault-detection
state machine, and reports over a CAN bus — architected as real vehicle
firmware would be, but runs entirely on a laptop with **no hardware, no
kernel modules, and no external dependencies**.

Built to demonstrate the specific skill set automotive firmware roles screen
for: C, RTOS-style concurrency, CAN protocol design, real-time fault
handling, and hardware/software boundary design — not just "can write C."

## Why this project

Three things distinguish it from a typical embedded portfolio project:

1. **It's a real control problem, not a blinking LED.** A Kalman filter
   fusing Coulomb-counted current with OCV-derived voltage for SoC, a
   debounced/latching fault state machine, and a watchdog are all things a
   real BMS does and a review board will ask about.
2. **It's built to be ported, not just to run once.** The OS calls (`osal/`)
   and CAN transport (`can/can_hal.h`) are both behind clean interfaces. The
   host build implements them with pthreads and an in-process virtual bus;
   swapping in FreeRTOS and SocketCAN/a real CAN peripheral touches only
   those two files, not the application logic. See
   [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#portability).
3. **It's tested, not just demoed.** 53 unit tests cover the SoC math, every
   fault-state transition (including the fault-latch-to-SHUTDOWN path), and
   CAN frame packing/unpacking — see [`docs/TEST_PLAN.md`](docs/TEST_PLAN.md).

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
make test              # build and run all 53 unit tests
make run               # build and run the simulator, nominal scenario, 8s
make run SCENARIO=overvoltage    # inject a fault scenario
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
  can/      CAN HAL (virtual bus) + protocol pack/unpack (DBC-equivalent)
  bms/      Domain logic: cell model, SoC estimator, fault state machine
  tasks/    The 5 periodic tasks + their shared app_context_t
  main.c    Wiring and sim lifecycle
test/       53 dependency-free unit tests (no external test framework)
docs/       Architecture, CAN protocol spec, test plan, interview notes
```

## Roadmap / what a v2 would add

- Replace the pthread OSAL backend with real FreeRTOS (`osal_freertos.c`) and
  cross-compile for an STM32/ESP32 target with real ADC-driven cell sensing.
- Replace SocketCAN's absence on macOS with a Linux/Docker dev path that
  exercises a real `vcan0` interface via `can_hal_socketcan.c`.
- Extend the SoC Kalman filter (`src/bms/soc_estimator.c`) to a proper
  multi-state model (SoC + a current-sensor bias term), rather than a scalar
  filter with hand-tuned noise constants.
- CAN-FD support and ISO 26262-style fault severity classification.

See [`docs/INTERVIEW_NOTES.md`](docs/INTERVIEW_NOTES.md) for how to talk
through the design decisions in an interview.
