# ERTS thread-creation callbacks — 2026-11-26

The [standalone thread initializer](../../../ports/erts/README.md#creation-callbacks)
copies the pinned OTP 29.1.2 `ethr_init_data` callback layout before
creation starts. Prepare and parent callbacks run on startup. The child
installs its adapter identity and invokes its captured initialization
callback before publishing completion and entering user code.

Startup retains the prepared pointer and exact native worker in one
adapter record while waiting for that completion. The child publishes
before posting a retained private-CQ wake. Startup rechecks the completion
after unrelated wakes, then invokes parent cleanup. Parent cleanup also
runs after a rejected native start. Local preflight rejection invokes no
callbacks. Parent cleanup can overlap user entry, so the child hook must
finish using the prepared data before returning.

The initialization record is immutable after registration. Reentrant
startup create, join, detach, drain and registration return `EBUSY` during
creation. An accepted-start handshake error aborts with its record
retained. Adapter exit from a child initializer aborts before publishing
completion. Callbacks execute outside runtime guards and remain responsible
for the resources represented by their prepared pointers.

## Signed qualification

The two-CPU AArch64 QEMU synchronization campaign passed 28 signed cases
and three rejection controls. The archive-linked positive fixture checked
registration copying, rejection of replacement, and prepare/child/parent
ordering across normal return, explicit exit and detached shutdown drain.
Child callbacks used their adapter identity and copied creation name,
installed destructor-bearing TSD and copied their prepared data before
parent cleanup released it.

Each child sent an early startup wake and waited for a bounded ten-millisecond
interval before finishing initialization. Parent cleanup remained uncalled
until the child returned. Prepare and parent callbacks attempted reentrant
thread control and observed rejection without changing output values or
closing admission. All three workers joined before final disposal, with
TSD cleanup confirmed and zero remaining libc allocations.

The rejected-start fixture had a signed one-thread budget. Prepare ran,
native start rejected, and parent cleanup released its complete prepared
allocation and event without a child callback. The output identity stayed
unchanged. Shutdown fenced later creation and initialization. A separate
child-exit fixture emitted `thread exit during child initialization` and
C abort. A later signed C application completed successfully.

The separate worker-libc regression passed 32 signed cases and three
rejection controls, including the existing thread, detached, name,
explicit-exit and exit-handler fixtures under the new handshake.
Python syntax, Rust formatting and diff checks passed. Native Rust sources
were unchanged from the preceding read/write qualification; this increment
qualified the changed C adapter through the signed campaigns.

The [focused summary](../../plans/evidence/erts-thread-init/focused/summary.json)
records the current archive, member and source hashes, signed fixtures
and pinned upstream callback implementation. The
[focused serial excerpts](../../plans/evidence/erts-thread-init/focused/serial-excerpts.txt)
retain ordering, admission rejection and fatal-control results. The
[regression summary](../../plans/evidence/erts-thread-init/regression/summary.json)
and [regression excerpts](../../plans/evidence/erts-thread-init/regression/serial-excerpts.txt)
retain the repeated earlier contracts.

## Remaining integration

`knytos_ethr_threads_init` is a standalone adapter initializer. Full
upstream `ethr_init` and `ethr_late_init` also establish runtime information,
thread-specific events and other state; those contracts remain open.
Actual OTP header selection, allocator hooks, atomics, thread progress
and coordinated termination still need integration.

Child initialization must make progress independently of parent cleanup
and return or abort. Startup cannot drain while its creation callback is
active. The handshake preserves ownership on uncertain completion; it
adds no bounded termination guarantee. This campaign did not inject
handshake wait-admission rejection or callback allocation failure. The
native start rejection was a known unstarted failure, with no replay of
accepted work. The earlier
[object-store fragmentation finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved and is preserved separately from these fresh-guest runs.

Repeat with new output directories.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-mutex --smp 2 --output target/erts/thread-init-validation
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/thread-init-regression
```
