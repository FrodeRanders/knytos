# ERTS TSD adapter — 2026-11-26

The [TSD adapter](../../../ports/erts/README.md#thread-specific-data-adapter)
provides the pinned OTP 29.1.2 thread library's four public keyed TSD
entry points over the Knytos C native-worker runtime. Keys retain
permanent identities and each participant has a separate borrowed value.
The adapter returns `EAGAIN` for key exhaustion, `EINVAL` for invalid
or stale set/delete operations, and null for an unset or stale get.
The identity overflow result maps to `EOVERFLOW`.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 24 signed cases and three
rejection controls. The standalone combined fixture checked all four
entry points, rejected a seventeenth live key without changing its
output, and rejected stale identities after deletion and fresh allocation.

The fixture registered an internal event-state destructor through the
native key-creation boundary and used the ERTS TSD adapter to set its
value. A worker allocated a containing state, created and set its event,
and returned. The exit destructor observed a cleared key, waited on the
set event through the ERTS event API, destroyed the event and freed the
containing allocation. Exact join returned after cleanup.

A second generation on the same worker slot began with null values and
repeated event-state cleanup. Startup retained its own TSD value across
both workers. The fixture exited with zero remaining libc allocations.

The four declarations were compared with
`erts/include/internal/ethread.h` at OTP commit
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`. Python syntax and diff
checks passed. The [run summary](../../plans/evidence/erts-tsd-adapter/summary.json)
identifies adapter sources, upstream headers and signed artifacts.
The [serial excerpts](../../plans/evidence/erts-tsd-adapter/serial-excerpts.txt)
retain the combined fixture's completion and allocation result.

## Remaining integration

The fixture combines the adapter components in a standalone native
executable. The full ERTS thread backend must select their types and
entry points, register cleanup for its own internal event state, and
coordinate thread entry, progress and shutdown. Its configure thread-library
requirement remains open. This run exercised key-count exhaustion.
Exhaustion of the 64-bit identity counter was outside the fixture.

Repeat the qualification with a fresh output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/tsd-adapter-validation
```
