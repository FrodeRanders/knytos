# C read/write lock qualification — 2026-11-24

The prior mutex and condition work was committed and pushed as
`06eeb8fe453ec1dd0bcd5b00a341d0791b6f0151` on
`origin/feature/erlang-runtime`. This increment adds read/write locks to
the opt-in worker profile. The lock uses local tracked heap storage and
borrows the participant's existing private CQ for contention. See the
[C ABI and build contract](../../../ports/libc/README.md#restricted-native-worker-profile).

Read ownership permits nesting by the same participant, with one unlock
per acquisition. Write ownership is exclusive. Both modes validate the
roster generation at unlock, and the lock rejects upgrades and recursive
write acquisition. A queued writer excludes new readers. Existing readers
can nest, and competing writers can race to acquire the released lock.
There is no FIFO or bounded fairness guarantee.

Reader and writer waits register under the lock's metadata guard and
use one monotonic deadline. Unlock or waiter withdrawal publishes the
admission change before signaling any eligible private queues. If the
last queued writer departs, fresh readers can acquire while an earlier
reader still holds the lock. A CQ wake prompts a state recheck and carries
no ownership. The caller keeps the lock live through its wait.

## Verification

QEMU 11.1.3 with four AArch64 guest CPUs passed all 15 signed cases and
three build or signature rejection controls. This includes the default
C runtime, workers, mutex, condition and read/write lock cases. Startup
ran on LP 0 and the lock fixture pinned its callbacks to LP 1. The
descriptor allowed five threads to cover all four worker launches plus
startup, with at most two callbacks active at once.

The C fixture checks concurrent and nested readers, rejects upgrade and
unowned unlock, and refuses destruction while a reader owns the object.
Its writer timeout case starts a new reader while a writer waits and the
first reader holds the lock. The new reader's try-read must report busy.
The first reader stays active until the new reader completes. This means
the timed writer's departure must reopen reader admission and post a
wake before the new reader's own timeout.

The fixture gives the writer a 40 ms scheduling opportunity before
starting the reader because the C ABI has no observer for a lock's
private registration. A successful try-read before registration would
fail the test. The timed pause alone supplies no readiness evidence.

The final phase starts an exclusive writer against shared ownership,
checks that releasing one level of a nested read does not grant it the
lock, and releases the last read. The writer publishes a new value that
startup reads after the writer's release. Destruction returns the local
allocation. See the [native fixture](../../../ports/libc/tests/rwlock.c).

The signed fixture was 131,376 bytes with SHA-256
`0074a1d46940172912624413f7cdfe8d062e9cb6b29aa580ad8635cb65d12712`.
The selected kernel SHA-256 was
`3575b3ce349c0b78d18ac474caa30c18d9e1181c5c54806163119d9af4396cba`.
89 host Rust tests and 30 boot and CI input checks passed, as did target
Clippy with warnings denied and workspace formatting. The native
case covers timed writer withdrawal. It did not exhaust waiter
admission or force numeric thread ID reuse. See the
[manifest](../../plans/evidence/erlang-rwlock/summary.json) and
[serial capture](../../plans/evidence/erlang-rwlock/run-1/serial.txt).

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --native-workers --smp 4 --output target/c-runtime/rwlock-validation
cargo clippy --locked -p knytos-cabi --release --features native-workers \
  --target crates/knytos-services/aarch64-unknown-none.json \
  -Z build-std=core,alloc,compiler_builtins -- -D warnings
cargo fmt --all -- --check
```

## AtomVM integration boundary

In the pinned release, blocking shared and exclusive operations guard
the VM's synchronized lists and module index. The process-table try-read
path found in the source is gated on the task driver. The C ABI now
supplies the lock operations. The adapter still needs to map failures from
finite waits to the VM's synchronization contracts. The remaining
integration also includes Picolibc reentrancy, worker launch from
scheduler context and transfer of the elected reactor role. See the
[multicore plan](../../plans/erlang-m3-multicore.md).
