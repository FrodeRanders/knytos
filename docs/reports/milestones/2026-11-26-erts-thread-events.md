# ERTS thread-specific event ownership — 2026-11-26

The [thread-event adapter](../../../ports/erts/README.md#thread-specific-events)
provides the pinned OTP 29.1.2 public fields, flags and nine lifecycle
entry points. Startup reserves a destructor key through a standalone
initializer. Each participant then owns a generation-qualified context
containing its cached object and temporary objects, with a sixteen-object
bound. The archive now contains seven separately compiled adapter objects.

A duplicate constructs its own native event and copies borrowed queue
metadata, flags and scalar state separately. It preserves upstream peek
flags while giving the native event independent readiness. Busy, peeked
and nonempty-user-data objects remain retained. Preparation failures
preserve the caller's output and existing context. One temporary owner
holds every newly allocated component until publication or rollback.

Explicit clear can release an idle prefix and retain the exact remaining
graph on busy rejection. Usage counts decrease after actual object
release and provide no cleanup authority. Worker TSD destruction runs
outside runtime guards with the participant identity still available.
A live exit borrow aborts while retaining the graph. Later user
destructors may rearm a fresh context for the next bounded native pass.
Startup explicitly clears its own context after last use.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 20 signed cases and three
rejection controls. The positive fixture checked:

- Independent native owners and reset state when duplicating a set event,
  while preserving borrowed user data, atomic flags and scalar fields.
- Busy rejection, partial clear preserving the busy tail, both release
  orders for copied busy/peek claims, and sixteen-object capacity rejection
  without changing the output pointer.
- Three simultaneous worker contexts, cross-CPU producer delivery, exact
  joins and a subsequent detached generation drained at shutdown.
- Exit-handler access before TSD cleanup, explicit exit, and fresh context
  rearming by a later user destructor after the earlier native key cleared.

The producer finished setting each worker's embedded event before opening
its separate entry gate. Thus event readiness was not used as permission
to destroy an object while that producer call still ran. Startup retained
all shared inputs and synchronization storage through exact joins and
drain, then cleared its own context. Final usage and libc-allocation
counts were zero.

The key-admission fixture filled all sixteen native keys, observed
`EAGAIN` from initialization, retired one key and initialized successfully.
It created and cleared an event context, then released its remaining user
keys with zero libc allocations. The busy-exit fixture retained a get
claim across callback return and emitted `thread event exit with live
borrow` followed by C abort. A later signed C application completed.

The same campaign retained the baseline native worker, timed mutex/condition,
read/write and manual-event cases. Native Rust sources and the existing
thread adapter were unchanged. Python syntax and diff checks passed.
The profile runs separately from the growing synchronization campaign
while the earlier object-store extent limitation remains unresolved.

The [summary](../../plans/evidence/erts-thread-events/summary.json) records
the seven-member archive, checked source hashes, signed fixtures and
pinned upstream sources. The
[serial excerpts](../../plans/evidence/erts-thread-events/serial-excerpts.txt)
retain successful lifecycle, admission and fatal-control diagnostics.
The [link-map excerpts](../../plans/evidence/erts-thread-events/link-map-excerpts.txt)
identify the separately linked adapter objects.

## Remaining integration

Actual OTP header selection, early/late initialization, main-thread and
reader-group status, allocator hooks and foreign-thread support remain
open. This implementation uses bounded direct allocation rather than
OTP's event pool. Double-word atomics, actual stack boundaries, thread
progress and coordinated backend shutdown still need integration.

Callers retain all queue and user-data borrows and exclude concurrent
producers before destruction. Temporary pointers expire when their final
busy/peek claim releases them. This campaign did not inject heap-allocation
failure or native-event destruction rejection from an independently parked
foreign waiter. It qualifies the observed lifecycle paths, not all kernel
backing release or broader backend termination. The native four-pass
destructor limit still applies to repeated rearming.

The earlier
[object-store fragmentation finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains open. This fresh-guest campaign does not close that failed capture.

Repeat with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-tse --smp 2 --output target/erts/thread-events-validation
```
