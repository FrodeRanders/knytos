# ERTS detached-thread custody — 2026-11-26

The [bounded thread adapter](../../../ports/erts/README.md#detached-custody-and-shutdown)
now admits detached creation and startup conversion of a joinable thread
to detached. Detached identities reject public joins. The adapter retains
their native owners, callback records and arguments until an explicit
shutdown drain confirms native joins.

`knytos_ethr_threads_shutdown` fences new creation before draining both
joinable and detached records. Successful joins release their records.
Rejected observation leaves the same record and every remaining worker
retained for a later drain call. The caller publishes terminal predicates
and wakes before draining and retains arguments through success.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 26 signed cases and three
rejection controls. The detached fixture created two detached workers,
created and detached a third joinable worker, and rejected public joins
without changing result outputs. A fourth creation hit the roster limit.

Startup released the workers' event gates and observed their cleanup
notifications. Creation still returned `EAGAIN` before the drain.
The drain then confirmed all native joins. Each destructor had observed
the exact worker identity. New creation returned `ECANCELED` without
changing its output identity, and a repeated completed drain succeeded.
Worker calls to detach or initiate the drain returned `EACCES`.

The fixture destroyed every gate and notification event, deleted its
cleanup key and exited with zero remaining libc allocations. The existing
joinable thread, TSD and event fixtures also passed in the same campaign.
Python syntax, Rust formatting and diff checks passed.

The [run summary](../../plans/evidence/erts-detached-threads/summary.json)
records signed artifacts and source identities. The
[serial excerpts](../../plans/evidence/erts-detached-threads/serial-excerpts.txt)
retain admission, drain and completion markers.

## Remaining integration

Detached workers occupy their slots until explicit drain. Background
reaping and normal-operation reuse of detached slots remain open.
The roster still admits three workers with default stacks and empty names.
Full ERTS needs a broader thread roster, named threads, stack and exit
contracts, thread-progress integration, and backend lifecycle wiring.
This fixture exercised a successful drain. Native join-observation
rejection and recovery were outside its executed cases.

Repeat the qualification with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/detached-validation
```
