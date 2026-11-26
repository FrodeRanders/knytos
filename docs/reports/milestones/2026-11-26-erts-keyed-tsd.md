# ERTS prerequisite — keyed thread-specific data

The C native-worker profile now supplies a bounded keyed TSD primitive.
It admits sixteen live keys with permanently retired identities. Each
registered participant has one borrowed pointer per key. Callback return
clears that participant's values before removing its roster identity.
Startup retains the exact thread owner through join before slot reuse.

The table allocates no storage dynamically and invokes no destructors.
Callers retain pointees and coordinate deletion with key users. The
[API contract](../../../ports/libc/README.md#thread-specific-data)
defines null values, stale keys, admission rejection and identity overflow.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 18 signed cases and three
rejection controls. The worker-libc fixture established distinct values
for startup and a live worker under the same key. Startup deleted another
key while the worker waited on the existing stream lock. The worker then
observed null and rejected set on that stale key. A newly allocated key
had a distinct identity.

After exact join, a second callback on the same slot observed an unset
value. Startup's original value survived both callbacks. Clearing and
deleting keys succeeded. A seventeenth live key was rejected without
changing the caller's output, and all sixteen admitted keys were deleted.
The fixture exited with zero remaining libc allocations.

The host Rust suite passed 77 unit tests and one compile-fail doctest.
Rust formatting and Python syntax checks passed. The
[run summary](../../plans/evidence/erts-keyed-tsd/summary.json) identifies
the signed fixture, descriptor and source inputs. The
[serial excerpts](../../plans/evidence/erts-keyed-tsd/serial-excerpts.txt)
retain the fixture's cross-CPU output and completion.

## ERTS integration boundary

OTP 29.1.2's keyed TSD functions can build on this primitive. Its internal
thread-event key also installs an exit destructor in the pthread backend.
An ERTS backend still needs to own that event cleanup, adapt thread
creation and joining, and qualify event waits and shutdown. The configure
thread-library requirement remains unsatisfied. Compiler-generated ELF
TLS has its own loader and context requirements.

To repeat the signed qualification from the repository root, use a fresh
output directory and the pinned Picolibc source.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/tsd-validation
```
