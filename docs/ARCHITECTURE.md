# Architecture

## Goals that shaped the design

1. **Runnable without hardware.** A reviewer should be able to clone this and
   see it work in under a minute. Every module that would normally touch a
   peripheral sits behind an interface with a host-only implementation.
2. **Portable to a real target with minimal churn.** The application logic
   (`bms/`, `tasks/`) should not know or care whether it's running on
   pthreads-over-macOS or FreeRTOS-over-an-STM32. That constraint is what
   produced the OSAL and CAN HAL layers below — it's not decoration, it's
   the thing that makes the "host simulation" honest rather than a toy.
3. **Fail safe, not fail silent.** A real BMS that keeps running on stale
   sensor data or a hung task is a safety hazard, not a bug ticket. The
   watchdog task and the fault-latch-to-SHUTDOWN behavior exist because of
   this, not because "RTOS projects have watchdogs."

## Layers

```
tasks/   →  bms/, can/          (application: knows the domain, not the platform)
bms/     →  can/can_protocol.h  (domain: knows message *meaning*, not transport)
can/     →  osal/               (transport: knows bytes-on-a-bus)
osal/    →  pthreads            (platform: knows the OS)
```

Each arrow is a compile-time `#include` of a header, never of an
implementation file. That's what makes swapping `osal_posix.c` for
`osal_freertos.c`, or `can_hal_virtual.c` for `can_hal_socketcan.c`, a
link-time decision instead of a rewrite.

## Concurrency model

Five periodic tasks share one `app_context_t`, guarded by a single mutex
(`src/tasks/app_context.h`):

| Task     | Period | Responsibility |
|----------|--------|-----------------|
| sensor   | 100ms  | Steps the simulated cell model, publishes the latest reading |
| soc      | 500ms  | Coulomb-counts SoC from the latest reading, updates cached status |
| fault    | 50ms   | Evaluates the fault state machine against the latest reading |
| can      | 100ms  | Publishes status/cell-voltage/fault CAN frames; handles inbound charge commands |
| watchdog | 200ms  | Checks every task's heartbeat; forces a fault if one goes stale |

**Why one coarse lock instead of per-field locking or lock-free structures:**
the critical sections here are a handful of struct-copy assignments, not
long-running work, so contention is not a real risk at these periods (max
20 Hz). Per-field locking would add complexity without a measurable benefit.
This is also the more common real-world choice in small ECU firmware for the
same reason — it's easy to reason about and audit for deadlocks (there's
only one lock; it's never held across a blocking call).

**Why the fault task runs fastest (50ms) and soc slowest (500ms):** fault
detection is the safety-critical path — a real overvoltage/overtemp
condition should be caught within tens of milliseconds, matching what a
real BMS's protection IC does. SoC is informational, not safety-critical, so
a slower, steadier estimate is fine and reduces CAN bus load.

## Fault state machine

```
                 limit exceeded
   ┌────────┐ ─────────────────▶ ┌───────┐
   │ NORMAL │                    │ FAULT │
   │   /    │ ◀───────────────── │       │
   │WARNING │  OK for            └───┬───┘
   └────────┘  FAULT_DEBOUNCE_MS     │
                                     │ 3rd FAULT entry within
                                     │ FAULT_LATCH_WINDOW_MS
                                     ▼
                              ┌──────────┐
                              │ SHUTDOWN │  (latched — no auto-recovery,
                              └──────────┘   models "open contactors,
                                              wait for service")
```

- **WARNING** is a soft zone inside the hard limits (`BMS_WARNING_MARGIN_MV`)
  so a state change is visible on the bus before a real fault trips —
  useful telemetry, not itself a protective action.
- **FAULT** entry is immediate on any hard limit violation (overvoltage,
  undervoltage, overtemp, cell imbalance) or a watchdog timeout. No
  debounce going *into* FAULT — a real cell that hits 4.2V doesn't get the
  benefit of the doubt.
- **Recovery** out of FAULT requires `FAULT_DEBOUNCE_MS` of continuously OK
  readings — this is standard hysteresis, and it exists so a reading that
  flickers exactly at a threshold doesn't chatter the state on every cycle.
- **SHUTDOWN is latched.** Three separate FAULT episodes within
  `FAULT_LATCH_WINDOW_MS` means the pack is unstable, not just unlucky once
  — the firmware stops trying to self-recover and requires an external
  reset, mirroring how real packs escalate to requiring a service tool.

Implementation: `src/bms/fault_manager.c`. Test coverage for every edge of
this diagram, including the 3-strikes latch: `test/test_fault_manager.c`.

## Portability

This is the part worth walking an interviewer through directly, because it's
the difference between "I wrote some C" and "I designed firmware."

### OSAL → FreeRTOS

`src/osal/osal.h` is the entire interface the application uses. The mapping
to real FreeRTOS calls is direct:

| OSAL call              | FreeRTOS equivalent                          |
|-------------------------|-----------------------------------------------|
| `osal_task_create`      | `xTaskCreate`                                  |
| `osal_task_delay_ms`    | `vTaskDelay(pdMS_TO_TICKS(ms))`               |
| `osal_get_tick_ms`      | `xTaskGetTickCount() * portTICK_PERIOD_MS`    |
| `osal_queue_create/send/receive` | `xQueueCreate` / `xQueueSend` / `xQueueReceive` |
| `osal_mutex_create/lock/unlock`  | `xSemaphoreCreateMutex` / `xSemaphoreTake` / `xSemaphoreGive` |

An `osal_freertos.c` implementing this table is the only new file a port
needs; nothing in `tasks/`, `bms/`, or `can/` changes.

**A real portability pitfall already hit within `osal_posix.c` itself:**
`clock_gettime`/`CLOCK_MONOTONIC`/`CLOCK_REALTIME`/`nanosleep` are POSIX
realtime extensions; glibc only declares them under strict `-std=c11` if a
feature-test macro (`_POSIX_C_SOURCE`) is defined before any system header
is included. macOS's libc exposes them regardless of the standard mode, so
this compiled and ran fine on every local (macOS) dev machine while failing
CI's `ubuntu-latest` job on every single commit, unnoticed, until CI status
was actually checked. The fix is one line (`#define _POSIX_C_SOURCE 199309L`
at the top of the file), but the lesson is the point: a host OSAL backend
being "portable" across POSIX systems is a claim, not a given, and it's only
verified by actually building on the other POSIX system, not by assuming
one Unix-like libc behaves like another.

### CAN HAL → SocketCAN / real peripheral

`src/can/can_hal.h` exposes `can_hal_init/send/subscribe/shutdown`. The host
build's `can_hal_virtual.c` is an in-process broadcast bus (see its header
comment for exactly what it does and doesn't model about real arbitration).
A Linux target implements the same four functions against a `PF_CAN` /
`SOCK_RAW` socket on `can0`/`vcan0`; an MCU target implements them against
the chip's CAN peripheral driver (e.g. STM32 bxCAN). `can_protocol.c` and
every task that calls `can_hal_send` are unaffected either way.

### What's intentionally *not* abstracted

The cell model (`src/bms/cell_model.c`) is not behind an interface — on real
hardware it would be replaced by an AFE driver (e.g. an LTC6811/BQ76952 SPI
driver) rather than ported, since a simulated pack and a real one don't
share meaningful logic. Abstracting it here would be complexity with no
payoff, which is why it's a direct, swappable module instead of a HAL.

## Future work (see also README "Roadmap")

- CAN-FD and larger pack sizes (this demo hardcodes a 4S pack for clarity).
- ISO 26262-style fault severity/ASIL classification instead of a flat
  bitmask.
