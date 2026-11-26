# ERTS prerequisite — worker TSD destructors

The native worker trampoline now invokes optional TSD destructors after
the C callback returns and before removing the participant's roster
identity. Each captured value is cleared before its destructor runs.
Callbacks execute outside the TSD table and worker roster guards, while
startup retains the callback argument and exact worker owner through join.

The [C API](../../../ports/libc/README.md#thread-specific-data) allows four
cleanup passes for rearmed values. Remaining destructor work aborts the
domain. Key deletion suppresses callbacks that cleanup has yet to capture.
Callers retain callback code and pointees through in-flight cleanup.
Startup TSD requires explicit cleanup before terminal C exit.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 21 signed cases and three
rejection controls. The cleanup fixture verified that each destructor
saw a cleared key while its worker identity remained registered.
The destructor created and deleted another key and observed a set native
event. It rearmed its value for four passes, then destroyed the event.
Exact join completed after cleanup.

A second generation reused the same worker slot with an initially null
value and repeated the cleanup. The startup value remained available.
Deleted and empty keys invoked no destructor. The fixture exited with
zero remaining libc allocations.

The bounded-failure fixture rearmed its value on every invocation.
The runtime emitted `TSD destructor iteration limit` and aborted that
domain. A subsequent signed C application launched and completed.
The host Rust suite passed 77 unit tests and one compile-fail doctest.
Rust formatting and Python syntax checks passed.

The [run summary](../../plans/evidence/erts-tsd-destructors/summary.json)
records both signed cleanup fixtures and their source identities. The
[serial excerpts](../../plans/evidence/erts-tsd-destructors/serial-excerpts.txt)
retain normal cleanup and the bounded-failure diagnostic.

## ERTS integration boundary

The primitive provides a worker-exit hook for an ERTS adapter's internal
event-state cleanup. ERTS thread entry, its nanosecond wait interface,
thread-progress bookkeeping and configure backend still need integration.
The native C worker roster remains bounded to four participants.

Repeat the signed qualification with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/destructor-validation
```
