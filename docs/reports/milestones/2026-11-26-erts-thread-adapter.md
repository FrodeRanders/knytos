# Bounded ERTS thread adapter — 2026-11-26

The [thread adapter](../../../ports/erts/README.md#bounded-thread-adapter)
provides startup-only create and join over three native worker slots.
Static records retain callback code, borrowed arguments, identities and
return pointers until exact native join. A rejected join leaves the
same record and native owner available for observation.

Identities include a generation and stay distinct across slot reuse.
The adapter supports self identity and comparison and rejects detach,
explicit stack suggestions and nonempty names with `ENOTSUP`.
The qualified profile uses default signed stack limits and LP 1 on a
guest with at least two CPUs.

## TSD cleanup ordering

Ordinary TSD values now remain available through every destructor pass.
Each destructor's own value still clears before invocation. Final clearing
occurs before removing the participant identity. This lets an exit
destructor call `ethr_self` even when its identity key was allocated before
the destructor key.

The adapter reserves one ordinary native TSD key for the domain lifetime.
Startup retains each exact worker and record through exit cleanup and join.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 25 signed cases and three
rejection controls. The thread fixture occupied all three slots and
rejected a fourth creation without modifying its output. Workers observed
their exact identities, rejected nested create and join, and returned
their original argument pointers. Exit destructors called `ethr_self`
successfully before join completed.

The fixture allocated the identity key before the destructor key to
exercise the cleanup ordering. It then reused a slot, verified a new
identity, rejected a join using the previous generation and left that
rejected join's result output unchanged. All gates were destroyed and
the fixture exited with zero remaining libc allocations.

The host Rust suite passed 77 unit tests and one compile-fail doctest.
Rust formatting, Python syntax and diff checks passed. The
[run summary](../../plans/evidence/erts-thread-adapter/summary.json)
records signed artifact and source identities. The
[serial excerpts](../../plans/evidence/erts-thread-adapter/serial-excerpts.txt)
retain the thread fixture's completion.

## Remaining integration

This qualification uses a standalone native fixture. Full ERTS requires
more threads and uses detached, named scheduler threads with stack
suggestions. Extending ownership and admission for those paths, explicit
thread exit, stack limits, thread-progress integration and cooperative
shutdown remains backend work. Rejected native join observation and
64-bit generation exhaustion were outside this fixture.

Repeat the qualification with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/thread-adapter-validation
```
