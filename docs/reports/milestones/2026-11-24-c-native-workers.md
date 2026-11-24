# Restricted C worker qualification — 2026-11-24

The prior M3 semantic qualification was committed and pushed to
`origin/feature/erlang-runtime` as `721d12d5af4d0ba9da1bec641c0f9ffdc59b2ff2`.
The next slice adds an opt-in C worker profile over existing Knytos thread,
completion and CQ facilities. It qualifies an owned worker lifecycle and
private waits across two guest CPUs. AtomVM still uses the single-scheduler
profile. See the [worker ABI and build instructions](../../../ports/libc/README.md#restricted-native-worker-profile).

## Worker ownership

The Rust roster reserves one of three worker slots before invoking the
existing spawn operation. Each slot has its own fixed trampoline. The
callback can run before spawn returns to startup. Its argument remains
borrowed from C until startup observes exit of the exact generation through
`ThreadHandle::join_preserving`. On join rejection the roster keeps the same
handle and callback custody. There is no C-visible kernel handle. A rejected
launch clears the unused start record so the caller can reuse the slot.

The worker returns to the trampoline and stops advertising its TID before
terminal thread exit. This avoids an exited slot matching a recycled TID.
The kernel can finish stack reclamation after its exit observation. The
profile exposes launch rejection separately from confirmed join and does
not infer capacity from join. The C exit boundary closes start admission
before finalizers. Any unjoined worker aborts the domain before finalizers or
heap cleanup. Existing kernel retirement owns physical stack disposal.

Startup and each worker use distinct private launch CQs. CQ zero is reserved
for the main reactor. Callbacks publish a synchronized predicate before
signaling and recheck the predicate after readiness. An early, stale or
coalesced wake gives only a reason to recheck. The checked wait preserves
deadline, admission failure and shutdown outcomes. These private waits are
finite because lifecycle wakes only the default CQ.

## Validation

QEMU 11.1.3 with two AArch64 guest CPUs passed 13 signed cases and
three negative build and signature controls. The campaign includes ten C
regressions and the following worker cases.

| Fixture | Signed thread ceiling | Observed result |
| --- | ---: | --- |
| `c-workers` | 2 | Passed a deadline and self-signal before wait. LP 1 entered the worker callback and exchanged a predicate and CQ signals with LP 0. Invalid LP, occupied slot and third-thread admission rejected. Exact join completed. |
| `c-unjoined` | 2 | The worker entered on LP 1. Startup returned with it still registered. The runtime rejected exit, the kernel logged domain abort, and the next signed case launched. |
| `c-worker-reuse` | 9 | Eight workers started and joined sequentially in the same roster slot. Each completion covered the argument write for that worker. A finalizer's attempted worker start returned shutdown. |

The stack ceiling was 16 pages per thread. The nine-thread descriptor in the
reuse case budgets startup and all eight launches even if stack charges
overlap. The fixture executes one worker at a time. The run passed on kernel
`1b819fe415addcfa71376c29d619e0c4ebcfb3f12d6440342f1e0ba272e88c27`.
The [manifest](../../plans/evidence/erlang-workers/summary.json) records
artifact digests and tools. The [serial capture](../../plans/evidence/erlang-workers/run-2/serial.txt)
records the LP, ASID, worker and survivor observations.

The first run passed the handshake and quota rejection on four guest CPUs.
The fixture then asserted that it could start a replacement immediately
after join under a two-thread ceiling. The replacement start rejected.
Source inspection showed that thread retirement can notify the exit
observer before releasing the stack pair. The observed rejection is
consistent with a retained stack charge. The fixture did not capture a
kernel quota receipt for that particular rejection. The corrected campaign
split quota rejection from sequential reuse and reserved capacity for all
reuse attempts, with no polling for stack return. The first run is retained
separately with its debugger capture. See the [failed-run record](../../plans/evidence/erlang-workers/run-1/result.json).

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --native-workers --smp 2 --output target/c-runtime/worker-validation
cargo clippy --locked -p knytos-cabi --release --features native-workers \
  --target crates/knytos-services/aarch64-unknown-none.json \
  -Z build-std=core,alloc,compiler_builtins -- -D warnings
cargo fmt --all -- --check
```

89 host Rust tests and 30 boot diagnostics, boot profile and CI runtime input
checks passed. The Rust host tests exercise the existing runtime ownership
helpers. The worker entry and CQ behavior were exercised natively. Native
wait or join admission exhaustion and thread ID generation reuse were not
forced. The slot reuse case tests the adapter's logical slot, independently
of whether the kernel reuses a numeric TID during that run.

## Remaining AtomVM SMP work

The worker profile keeps Picolibc calls on startup. The positive native
callback uses atomics, clock, console and CQ operations over existing Rust
runtime facilities. AtomVM concurrency requires thread-safe C library state,
mutex and condition-variable semantics, and a policy for worker-originated
launch requests under its scheduler locks. The upstream main and worker
schedulers also need to hand off the sole reactor role. The current profile
does not qualify those paths. See the [multicore plan](../../plans/erlang-m3-multicore.md).
