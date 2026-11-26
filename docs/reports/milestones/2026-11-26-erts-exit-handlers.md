# ERTS exit handlers — 2026-11-26

The [exit-handler adapter](../../../ports/erts/README.md#exit-handlers)
provides `ethr_install_exit_handler` for up to sixteen callbacks
registered by startup. Entries are immutable after count publication.
Each worker exit captures a count and runs callbacks in reverse
registration order before native TSD teardown.

Normal callback return and explicit exit share this dispatch. Callbacks
run outside runtime guards while the worker's identity, name and TSD
remain available. A per-record marker prevents dispatch repetition during
subsequent TSD cleanup, and an exit request from a running handler aborts
the domain. Callback code remains borrowed through all retained exits.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 32 signed cases and three
rejection controls. The main handler fixture checked reverse registration
order before TSD destruction across normal return, explicit exit and
detached shutdown drain. Handlers used their worker TSD and identity,
and set an event that the TSD destructor subsequently observed and released.

The fixture checked invalid callback rejection, worker registration
rejection and registration rejection after the shutdown fence.
It destroyed every event, deleted its TSD key and exited with zero
remaining libc allocations.

A separate fixture filled all sixteen registrations and rejected the
seventeenth with `EAGAIN`. The recursive fixture requested exit from
a handler, emitted `recursive exit handler` and entered the C abort
path. A later signed C application completed successfully.

The [run summary](../../plans/evidence/erts-exit-handlers/summary.json)
records the three signed fixtures, adapter inputs and pinned upstream
sources. The [serial excerpts](../../plans/evidence/erts-exit-handlers/serial-excerpts.txt)
retain order, recursion and capacity markers. Python syntax, Rust
formatting and diff checks passed.

## Remaining integration

Registration is restricted to startup and fixed storage. Full ERTS must
install its concrete callbacks through this interface and qualify their
thread-progress and driver-resource cleanup. The adapter's broader thread,
stack, detached-reaping and lifecycle work remains open. Registration
overlapping a running exit snapshot was outside this fixture.

Repeat the qualification with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/handler-validation
```
