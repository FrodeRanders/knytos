# C mutex and condition qualification — 2026-11-24

The restricted worker profile was committed and pushed to
`origin/feature/erlang-runtime` as `3150fb40e3669ed33f5ce4db7ee8271904f77915`.
The next increment adds nonrecursive mutexes and bounded condition waits
to its opt-in C ABI. These objects use ordinary local heap storage. They
contain no kernel handles. See the
[C worker and synchronization contract](../../../ports/libc/README.md#restricted-native-worker-profile).

## Ownership and waits

- A mutex records its owner as a roster slot and a runtime generation. A
  replacement worker in the same slot has a different identity. The C
  callback releases any held mutex before returning.
- Contention registers against the same metadata that unlock changes.
  Condition wait registers while the caller still owns the mutex, then
  releases it. Signal moves the chosen pending waiter or waiters to selected
  state. Internal guards are released before notification.
- CQ readiness prompts an exact state check. A stale notification from an
  earlier operation or slot occupant cannot grant the mutex or select a
  condition waiter. The queue is launch-owned and private to the participant.
- The condition wait uses one monotonic budget for selection and
  reacquisition. After a valid call releases the mutex, only
  `KNYTOS_OK` conveys ownership again. Timeout or wait failure removes
  the waiter and leaves the mutex released. This contract differs from a
  POSIX condition wait that reacquires its lock on timeout.
- The caller keeps both objects live through wait and reacquisition and
  excludes concurrent use before destroying them. Busy destruction
  retains the same allocation. Any residual plain metadata belongs to
  terminal C heap disposal after all worker joins.

## Tested behavior

QEMU 11.1.3 passed 14 signed AArch64 cases and three build or signature
rejection controls in each run. The first run used two guest CPUs. The
expanded run used four and added the shared-deadline and former-owner
probes. Both runs include the ten default C cases and three worker cases.
The synchronization fixture runs the startup thread on LP 0 and pins
its callbacks to LP 1. Its nine-thread ceiling allows headroom for all
eight sequential or overlapping worker generations. The fixture has at
most two callbacks active at once.

The fixture exercises try-lock under contention, recursive acquisition
rejection, wrong-owner unlock, refusal to destroy a held mutex, expiry,
and visibility of a payload written by the previous mutex owner. It checks
that signaling an empty condition leaves no token. Two worker callbacks
register on the same condition. A manual private CQ hint to the second
worker cannot stand in for selection. The first round selects one waiter,
then releases the other. A second round broadcasts to both pending waiters.

The expanded run spends 300 ms of a 450 ms condition budget before
signaling. Startup holds the mutex until the waiter returns. The fixture
requires deadline status and verifies that the worker did not own the
mutex on return. Its accepted elapsed window is 450 to less than 650 ms,
below the roughly 750 ms required by a fresh budget at reacquisition.
The following misuse probe leaves a mutex held when one worker exits.
Its replacement in the same roster slot cannot unlock the old mutex and
times out trying to acquire it. Normal exit reclaims that final local
allocation after both joins.

The expanded signed fixture was 136,256 bytes with SHA-256
`82eb98ec26fe67bb98096ac882ffddbe4dd014e1dd0306eecb038a9ab0bd2a56`.
Its kernel SHA-256 was
`dd3a895ad6ff450185e308068f9655e0ea9e361ff8fde588e335cb6ffbecf3fb`.
The evidence keeps both runs and their distinct build inputs. See the
[manifest](../../plans/evidence/erlang-sync/summary.json) and
[expanded run serial](../../plans/evidence/erlang-sync/run-2/serial.txt).

The campaign uses the [C fixture](../../../ports/libc/tests/sync.c) and
[signing harness](../../../ports/libc/run-tests.py).

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --native-workers --smp 4 --output target/c-runtime/sync-validation
cargo clippy --locked -p knytos-cabi --release --features native-workers \
  --target crates/knytos-services/aarch64-unknown-none.json \
  -Z build-std=core,alloc,compiler_builtins -- -D warnings
cargo fmt --all -- --check
```

89 host Rust tests and 30 boot diagnostics, boot profile and CI runtime
input checks passed. Target Clippy passed with warnings denied. The host
tests cover the existing ownership and wait helpers. Scheduler contention
and condition behavior are exercised by the signed native fixture.

## Remaining integration

The condition API qualifies a finite wait on a private queue. Private
waiter admission exhaustion and lifecycle shutdown during a parked wait
remain unforced native scenarios. The kernel's lifecycle wake still targets
CQ zero. AtomVM SMP still needs read/write locks, an adapter for its wait and
reacquisition contract, thread-safe libc, and the elected reactor handoff.
The AtomVM build still uses one native scheduler. See the
[multicore plan](../../plans/erlang-m3-multicore.md).
