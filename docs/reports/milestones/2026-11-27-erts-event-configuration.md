# ERTS event allocator and main status — 2026-11-27

The [event-side late initializer](../../../ports/erts/README.md#event-late-configuration)
accepts the pinned OTP 29.1.2 allocator/count layout and copies options
before publishing its reserved destructor key. This is a standalone
event initializer; full upstream `ethr_late_init` remains open.

Context and list-node allocations use `mem.std`. An incomplete standard
tuple selects all three default malloc/realloc/free callbacks together,
matching upstream's fallback. Incomplete short-/long-lived groups inherit
the standard tuple, though this adapter has no consumers for those groups.
Native events retain their existing C-runtime allocation path.

Each context captures its original allocator through preparation rollback,
node disposal, clear and worker destruction. Callbacks run outside runtime
guards and supply aligned storage and consuming local deallocation. They
must return normally and retain their code/backing for the domain lifetime.
Per-participant guards reject reentrant owner operations and status mutation.
Read-only status and usage snapshots remain available during callbacks.

The two upstream main-status entry points use the current cached event.
Get allocates nothing and reports off if no cache exists. Set requires a
cached event and a positive index within the captured main-thread bound.
Disable clears the flag and index. This profile supports at most four
logical main slots, matching its startup-plus-three-worker roster. These
flags do not alter kernel admission, placement or trusted classification.

## Signed qualification

The two-CPU AArch64 QEMU event campaign passed 22 signed cases and three
rejection controls. The new custom-allocator fixture checked:

- Copied callback/count configuration after mutation of caller storage.
- Context allocation failure and subsequent node allocation failure, with
  unchanged outputs, no published event and complete prefix disposal.
- Failed temporary allocation with the existing context and cached owner
  retained unchanged.
- Matching tagged deallocation, rejected reentrant construction/clear/status
  mutation, and successful read-only status queries during callbacks.
- Negative counts, unsupported main-slot counts, missing caches, index
  bounds, status disable and copied temporary status snapshots.

Two worker generations checked status isolation from startup, normal and
explicit exit, and fresh status after the earlier event destructor cleared
its key. The later user destructor rearmed a context and marked its status;
the following native pass disposed it through the captured allocator.
Every observed join ended with zero live custom allocations and zero
thread events. Final libc allocation count was zero.

The fallback fixture supplied an incomplete tuple containing trap callbacks.
It completed through the whole standard fallback without invoking those
traps. Existing owner, busy/peek, capacity, key-rejection, destructor-rearm
and live-borrow-abort fixtures passed again, along with baseline native
worker and synchronization cases. A later signed C application completed.
Native Rust sources and the existing thread adapter were unchanged.
Python syntax and diff checks passed.

The [summary](../../plans/evidence/erts-event-configuration/summary.json)
records checked source hashes, the seven-member archive, signed fixtures
and pinned upstream definitions. The
[serial excerpts](../../plans/evidence/erts-event-configuration/serial-excerpts.txt)
retain allocator and status results. The
[link-map excerpts](../../plans/evidence/erts-event-configuration/link-map-excerpts.txt)
identify the separately linked event/thread adapters.

## Remaining integration

Actual OTP header selection, full early/late initialization, CPU information,
reader-group consumers, double-word atomics, actual stack boundaries, thread
progress and coordinated backend termination remain open. Event metadata
uses fixed-size allocations, so the captured realloc callback is not
invoked. This qualifies the standard event allocator, not the complete ERTS
allocator subsystem.

The fixture injected failures in custom metadata allocation, not native
event allocation or TSD publication. Allocator callbacks must obey their
normal-return and local-release contract; this work does not make arbitrary
callbacks or nonlocal exits safe. Temporary status is a snapshot rather
than a second ownership or classification authority. The earlier
[object-store fragmentation finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved and separate from this fresh-guest passing campaign.

Repeat with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-tse --smp 2 --output target/erts/event-configuration-validation
```
