# ERTS prerequisite — native manual-reset events

The C native-worker profile supplies an event with a retained set state,
explicit reset and one admitted waiter. Registration and readiness checks
share the event's metadata guard. Set publishes readiness before notifying
the captured participant's private CQ, after releasing that guard.

Timed waits retain one monotonic deadline across unrelated notifications.
An indefinite wait requires an explicit producer and terminal wake path.
Wait rejection and shutdown keep their distinct C statuses. The caller
retains event storage through every call and synchronizes destruction
with producers and waiters. The [API contract](../../../ports/libc/README.md#manual-reset-events)
defines reset races, waiter admission and destruction.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 19 signed cases and three
rejection controls. The event fixture checked initially reset state,
invalid arguments, repeated set, early set, explicit reset and successive
waits on the retained set state.

A worker sent ten unrelated private-queue hints, separated by 30 ms fixture
timers. Startup's 100 ms event wait timed out within the fixture's original
220 ms ceiling. The worker then published a payload and set the event.
Startup received the event and observed the payload before joining the
worker. A second callback exercised an indefinite wait on the same event,
followed by exact join, reset, another timeout and explicit destruction.
The fixture exited with zero remaining libc allocations.

The host Rust suite passed 77 unit tests and one compile-fail doctest.
Rust formatting and Python syntax checks passed. The
[run summary](../../plans/evidence/erts-native-events/summary.json)
records the measured timeout, signed artifact identities and source hashes.
The [serial excerpts](../../plans/evidence/erts-native-events/serial-excerpts.txt)
retain the event markers and C runtime exit.

## ERTS integration boundary

OTP 29.1.2 uses retained events with one waiter in its thread library.
The native primitive provides millisecond waits. An ERTS backend must
convert the upstream nanosecond budget, map wait failures and own event
destruction through thread exit. Thread creation, stack admission and the
configure thread-library requirement remain integration work.

Repeat the signed qualification with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/event-validation
```
