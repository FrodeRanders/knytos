# ERTS event adapter — 2026-11-26

The [native event adapter](../../../ports/erts/README.md#native-event-adapter)
implements the nine event entry points declared by the pinned OTP 29.1.2
thread-library header. It uses the qualified Knytos C worker event
primitive and supplies retained readiness, reset and memory barriers.

Positive nanosecond timeouts round up to milliseconds without addition
overflow. Zero observes readiness immediately. Every negative timeout
selects an indefinite wait. A nonnegative spin count goes directly to
the native wait. Timed waits return zero or `ETIMEDOUT`. Unexpected
native errors abort the domain.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 23 signed cases and three
rejection controls. The standalone adapter fixture checked a one-nanosecond
timeout that waited at least one millisecond, zero timeout, persistent set
state, reset, and `INT64_MAX` and `INT64_MIN` timeout inputs on a set event.

A worker published a payload and set the event after a bounded delay.
Startup's indefinite ERTS event wait returned and observed the payload.
The fixture joined the worker, destroyed the event and rejected subsequent
prepare/destroy calls. It exited with zero remaining libc allocations.

The invalid-input fixture supplied a negative spin count. The adapter
reported `event operation failed` and entered the C abort path. The signed
runner subsequently launched another successful C application.

All nine declarations were compared with
`erts/include/internal/pthread/ethr_event.h` at OTP commit
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`. Python syntax and diff
checks passed. The [run summary](../../plans/evidence/erts-event-adapter/summary.json)
records upstream hashes, adapter inputs and both signed fixture identities.
The [serial excerpts](../../plans/evidence/erts-event-adapter/serial-excerpts.txt)
retain normal completion and the fatal invalid-input path.

## Remaining integration

The fixture compiles the adapter into a standalone native executable.
The enclosing ERTS thread backend still needs to select its event type,
link the implementation, and connect event cleanup to worker-exit TSD.
Cooperative shutdown must publish terminal events and join every waiter.
The configure thread-library requirement remains open.
