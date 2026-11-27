# Static C applications on Knytos

This is the M1 AArch64 C11 profile: one native thread, no ELF TLS, and a
statically linked Picolibc subset over the existing Rust runtime. It uses
kernel runtime ABI 4 and C API version 1. AtomVM integration is separate.

## Build

Install Clang, CMake, Ninja and the repository's pinned Rust toolchain with
its LLVM tools. From the repository root:

```sh
python3 ports/libc/build.py ports/libc/tests/suite.c \
  --fetch --output target/c-runtime/hello.elf
```

`--fetch` clones Picolibc into `.dependencies/picolibc` if absent. Alternatively
pass `--picolibc-source /path/to/picolibc`. The source must be clean at 1.8.12,
commit `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. The build uses single-thread
mode, global errno, no TLS, and the Rust allocator in place of libc malloc.
The selected libc functions and Rust compiler builtins link into one ELF;
there is no `sbrk`, dynamic linker or second heap.

The output is unsigned. Its sibling `.build.json` records compiler versions,
source/input hashes and the unsigned ELF hash. `picolibc-COPYING.txt` contains
the upstream license inventory; retain it with distributed artifacts and
consult the pinned source for individual file notices. No upstream source
patches are required. Signing changes the ELF hash.

Use the normal artifact signing and authenticated deployment process. The
test harness demonstrates `cluster-sign elf-sign`, `deployment-sign` and
`deployment-notify`; its development fixture key is not a production key.
The kernel independently checks signatures, ABI, mappings and signed resource
limits. Build checks reject TLS, runtime relocation sections, dynamic program
headers and invalid or writable/executable LOAD layouts.

## Runtime contract

The Rust `_start` validates the launch context and initializes the standard
heap status record. Loader-provided data and BSS are retained. Constructors
run in ascending priority before `main(1, argv)`, with `argv[0] = "knytos-c"`
and a terminating null pointer. There is no environment or argument parser.
Constructor boundaries may coincide when no constructors are linked.

`malloc` and `calloc` return independently freeable blocks even for zero size.
`free(NULL)` does nothing. `realloc(p, 0)` frees `p` and returns null; other
failed reallocations preserve `p`. Arithmetic overflow and allocator exhaustion
return null with `ENOMEM`. `aligned_alloc` requires a nonzero power-of-two
alignment and a size divisible by it, otherwise `EINVAL`; successful realloc
preserves the original alignment. Only live pointers from this allocator may
be freed or resized, exactly once. The private list retains each allocation's
original Rust layout until release. It owns heap bytes, not capabilities.

`stdout` and `stderr` share a bounded console buffer, flushed on newline,
capacity or explicit flush into length-delimited Knytos log records. There
is no terminal or file descriptor namespace. `stdin` returns EOF with `ENOSYS`.
String, conversion, formatting and math functions come from Picolibc;
unsupported OS imports fail to link rather than return fabricated success.

`include/knytos.h` defines fixed-width statuses and C layouts for API version
1. Buffers must be valid in this native domain for the call's duration.
Output pointers must have their C type's alignment. Null pointers, oversized
console writes (over 65,536 bytes), and clock conversion errors return explicit
status. Monotonic time uses checked counter/frequency conversion. Realtime
returns `KNYTOS_UNAVAILABLE` without changing the output in the ordinary and
worker-libc profiles. The service-worker profile supports the explicit
signed-service query described below. Uptime is never Unix time.

### Authorized realtime queries

In the `--service-worker` profile, `knytos_clock_realtime_ns` acquires the
launch descriptor's `time=call` grant and queries `OP_NOW` for a fresh copied
UTC snapshot. Startup and registered workers each use their own private CQ;
the function does not consume the launch reactor's CQ-zero cursor. One
monotonic one-second budget spans grant acquisition and query observation.
This can block and is not an async-signal clock path. Accepted work settles
or follows the SDK's checked cancellation/fail-closed policy before its
connection is released; there is no automatic replay of an accepted query.
Grant preparation temporarily maps the launch-owned profile. The kernel
permits one mapping of that object per address space, so overlapping
preparation can return `KNYTOS_BUSY` before controller submission. This is
an unstarted admission rejection with unchanged output, distinct from an
unavailable or unsynchronized time source. The bridge does not spin or
silently retry that rejection.

The Rust query scope owns its connection and pending call. Returned memory
stays owned through bounded read-only mapping, validation and unmapping.
Unexpected returned connections/memory are also released through their
owners. No owning capability or pending operation crosses the C boundary,
and no connection or time sample is cached globally.

Only a synchronized, canonical 80-byte snapshot produces unsigned Unix
nanoseconds. Unsynchronized/holdover states report unavailable; malformed
frames, incoherent UTC/calendar data and unrepresentable timestamps reject.
The output is written only after successful cleanup and validation. Missing
grants, deadlines, rejected waits and shutdown retain distinct statuses.
The API supplies calendar time, not authenticated security time; the time
service's synchronized state does not authenticate its upstream NTP source.

```sh
python3 ports/libc/run-tests.py --c-clock --smp 2 \
  --picolibc-source /path/to/picolibc --output target/c-runtime/realtime-validation
```

The signed profile checks overlapping startup/worker attempts with explicit
busy rejection, an uncontended worker query, repeated reply cleanup, pointer
rejection and denial without `time=call`. Its baseline
clock case still requires ordinary-profile realtime to remain unavailable.
Host tests cover exact frame bounds, reserved bytes, schema/state rejection,
UTC consistency and nanosecond overflow. The ERTS clock backend and its
configure selection still need to be connected to this facility.
The [realtime qualification](../../docs/reports/milestones/2026-11-27-c-realtime.md)
records 19 passing signed cases, three deployment controls, seven host tests
and both original failed worker captures. Its
[manifest](../../docs/plans/evidence/c-realtime/summary.json) retains source,
artifact and descriptor identities and the observed mapping-admission limit.

### Caller-owned UTC samples and local projection

`knytos_clock_realtime_sample` uses the same authorized query, one-second
budget and complete native owners as `knytos_clock_realtime_ns`. On success
it copies a 48-byte `knytos_realtime_sample` containing UTC nanoseconds, the
service's matching monotonic ticks/frequency, drift estimate, original NTP
age and uncertainty. The explicit reserved field is zero. Both query APIs
reject a nonpositive drift-adjusted rate or a synchronized snapshot with
the never-synchronized age sentinel. Ordinary and worker-libc profiles
return unavailable, preserving the entire output structure.

`knytos_clock_sample_project_ns` takes an immutable sample, an explicit
maximum nominal elapsed time and disjoint output storage. It makes one local
counter query and projects elapsed time using the copied drift. It performs
no allocation, RPC, wait, retry, refresh or shared publication. All profiles
support this function. Exceeding the sample-age limit returns deadline;
changed frequency or a counter before the sample returns unavailable;
invalid metadata and checked arithmetic overflow remain distinct. Every
failure preserves output. It never substitutes uptime for missing UTC.

The limit measures time since the sample, not since upstream synchronization,
and does not bound UTC error. Original quality fields remain metadata about
the sample. Later service corrections or its nonregression clamp may make a
fresh service query differ from the estimate; integer re-anchoring can also
introduce nanosecond rounding differences. Projection does not assert ongoing
synchronization or authenticate time. Callers choose a validity policy and
keep each sample immutable through all concurrent reads; publishing a
replacement needs their own synchronization. There is no global sample cache.

The [sample qualification](../../docs/reports/milestones/2026-11-27-c-realtime-sample.md)
extends `--c-clock` to 21 signed cases and three deployment controls, including
cross-CPU local reads under a native mutex, age/frequency/overflow rejection,
whole-output preservation and grant denial. Twelve host tests cover layout,
metadata, arithmetic and frame validation. The standalone
[ERTS system clock](../erts/README.md#published-system-clock-prerequisite)
uses these primitives for bounded publication, explicit refresh and terminal
expiry. Its ERTS startup/refresh scheduling and native configure selection
remain open.

The single-threaded reactor API borrows launch CQ zero: `knytos_event_signal`
posts a retained work generation, and `knytos_event_wait` accepts -1 for an
indefinite wait or nonnegative milliseconds. It returns distinct readiness,
deadline, waiter rejection, invalid request and authenticated shutdown statuses.
It checks lifecycle before and after waiting. The caller must be the sole CQ
consumer, retain any accepted operation across all outcomes, and carry its
own absolute deadline across unrelated readiness. No capability is adopted or
result observed by this wait. The [AtomVM adapter](../atomvm/README.md) uses
it with no outstanding native service operations and aborts on wait errors.

Returning from `main` calls `exit`. Exit callbacks registered with `atexit`
run before destructors, then destructors run in reverse priority. Rust flushes
output, releases all remaining C heap blocks and logs the C result, released
block count, remaining count, runtime heap peak and sampled stack depth.
Zero exits the sole thread; nonzero records the C status and aborts the domain.
There is no kernel C process exit-status API. `_Exit` skips C callbacks while
performing terminal heap cleanup; `abort` skips normal exit and invokes domain
abort. Recursive exit aborts. Kernel retirement remains responsible for the
domain; local heap release is not a claim of domain retirement completion.

There are no C resource tokens, workers or asynchronous operations in this
profile. It borrows the launch context and does not adopt launch-owned
capabilities. Extending it with IPC requires actual Rust owners, consuming
transfer and confirmed completion/cancellation before release. Do not add
integer capability ownership or blocking cleanup to libc finalizers.

## Validate

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --output target/c-runtime/native-validation
```

The output directory must be new. The harness builds bundled AArch64 Rust
services with the standard runner, creates a fresh disk, and launches signed
C fixtures through TLS-authorized deployment. It uses localhost ports 28080
and 28081. It waits for boot, time and controller readiness before submission;
it does not retry accepted deployment mutations. Results correlate launch
ASIDs with output, and failed campaigns retain separate diagnostics. QEMU
needs permission to open the local ports and diagnostic sockets.

Fixtures cover ABI calls, allocation pressure, formatting/math, clocks,
constructor/exit ordering, deliberate leaked heap cleanup, nonzero return,
abort, empty callback arrays and a successful launch after failures. TLS and
unsupported imports must fail to build; tampering must fail signature checks.
Each descriptor permits one thread and 16 stack pages (64 KiB). Stack depth
sampled at selected Rust entry points is a lower bound, not a complete stack
high-water measurement or a VM stack budget. Heap pressure intentionally
approaches the existing 4 MiB heap limit; it does not establish a VM minimum.

TLS models, pthreads, SMP, POSIX processes/filesystems, general C IPC and JIT
execution are unsupported in the default profile.

## Restricted native worker profile

`build.py --native-workers` enables a separate C ABI for worker fixtures.
Picolibc still uses single-thread mode and global errno. Keep libc calls on
the startup thread. Worker callbacks may use C atomics, their own memory and
the documented Rust-backed clock, console and private wait operations. The
single-scheduler AtomVM build uses the default profile.

Only startup can start or join workers. It supplies a slot from 1 through 3,
a logical processor, a callback and an argument. The Rust roster prepares
the callback before submission and keeps the generation-bound thread handle
until successful join. C retains the argument and referenced data until
that join. Join confirms execution has ended. The kernel can still hold a
stack charge, so a subsequent start may return `KNYTOS_UNAVAILABLE` even
after the same slot became available.

### Thread-specific data

The worker profile provides `knytos_tsd_key_create`, `knytos_tsd_key_delete`,
`knytos_tsd_set` and `knytos_tsd_get` for up to sixteen live keys.
Each registered participant has an initially null value per key. Setting
a value stores a borrowed pointer. The caller retains the pointee and
synchronizes key deletion with its users.

Deleting a key clears all its values and retires its identity permanently.
Stale keys return `KNYTOS_INVALID` from set/delete and null from get.
Key exhaustion returns `KNYTOS_UNAVAILABLE` and leaves the output unchanged.
Identity exhaustion returns `KNYTOS_OVERFLOW`. Worker callback return
clears that participant's values before its exit, and startup retains the
exact worker owner through join before reusing the slot. The API runs
without allocation.

`knytos_tsd_key_create_with_destructor` optionally registers a callback
for a non-null worker value. The runtime clears the value before calling
its destructor after the worker callback returns. The participant remains
registered during cleanup, so destructors can use TSD and worker APIs.
Callbacks run outside table and roster guards. The caller retains callback
code and pointees through cleanup.

Values in ordinary keys remain available until all destructor passes
finish. This keeps participant identity and related borrowed context
available to destructors regardless of key allocation order.

Cleanup makes four passes to support destructors that rearm values.
Rearming beyond that bound aborts the domain. Deleting a key suppresses
destructors that cleanup has yet to capture. Callers synchronize deletion
and callback lifetime with every worker using the key. Startup values
require explicit cleanup before terminal C exit.

ERTS can use these primitives for its keyed TSD and thread-event cleanup.
The native thread backend still requires integration.

### Explicit worker exit

`knytos_thread_exit` is a terminal C boundary for a registered worker.
It runs the same TSD cleanup as callback return, clears participant identity
after cleanup and exits the native thread. Startup retains the exact worker
owner and argument borrow until successful join. Calls from startup and
recursive exit from a TSD destructor abort the domain.

The caller must leave no live Rust owner or guard that requires stack
unwinding above this boundary. The original C worker trampoline retains
its start metadata in a Copy-only Rust frame. Native capabilities remain
in their existing owners, and thread exit does not observe their completion.

### Manual-reset events

`knytos_event_create` allocates an initially reset event. The caller owns
its storage until `knytos_event_destroy` succeeds. Set persists across
waits until an explicit reset. The event admits one registered waiter,
and a competing waiter returns `KNYTOS_BUSY`.

`knytos_event_wait_on(Event, Milliseconds)` accepts -1 for an indefinite
wait or a nonnegative total timeout. A set event returns `KNYTOS_OK`,
including when the signal preceded the wait. A reset event with a zero
timeout returns `KNYTOS_DEADLINE`. Unrelated queue hints trigger a readiness
recheck with the remaining monotonic budget. Wait-admission rejection and
shutdown retain their distinct statuses. An indefinite wait requires a
producer and an explicit terminal wake path.

Retain the event throughout all calls. Destruction needs external lifetime
synchronization that excludes new calls and producer access. A registered
waiter makes destruction return `KNYTOS_BUSY`. Set publishes state before
notifying the captured waiter, after releasing the metadata guard. Reset
and set are serialized. A reset can clear a set before the waiter observes
it, so callers must coordinate resets with their protocol.

### Private waits

`knytos_mutex_lock_event` acquires a retained native mutex without a timeout.
It shares waiter registration and unlock notification with the timed mutex
API. The caller must arrange an unlock producer and a terminal path before
using it. Unrelated private-queue hints recheck ownership, and wait rejection
or shutdown returns its distinct status after removing waiter interest.

Each participant has a private launch CQ. Slot zero denotes startup, and
its private queue is distinct from the application reactor on CQ zero.
Publish an application predicate with appropriate synchronization before
`knytos_thread_signal` and recheck it after `knytos_thread_wait`. A wake can
arrive early, coalesce, or survive slot reuse. It carries no payload or exit
authority. By default, private waits check lifecycle before and after the wait.
Lifecycle wakes target CQ zero. A finite private waiter can remain
asleep until its deadline in the absence of a peer wake. Use
`knytos_thread_wait_event` only when the application has an explicit
terminal path for the event-only private waiter.

`knytos_event_wait` remains startup-only. A registered participant elected
as the application reactor can use `knytos_worker_event_wait` on CQ zero.
An overlapping borrow through either API returns `KNYTOS_BUSY`. The
application must publish poller handoff after the current wait returns.

`knytos_shutdown_requested()` checks the authenticated lifecycle state at an
application checkpoint and marks a pending request as seen. Runnable work
can keep a reactor from entering `knytos_event_wait`. The checkpoint borrows
launch state and leaves CQ readiness for the elected poller. Seeing the
request leaves the terminal cleanup transition pending.

Before submitting any worker, startup may call
`knytos_shutdown_via_reactor()` to route wait-time lifecycle delivery to
CQ zero. Private waits continue to serve peer predicates during shutdown.
The elected reactor must initiate the terminal path for those peers.
Use `knytos_shutdown_complete()` from startup after application work has
stopped, every worker has joined, and external resources are disposed.
The runtime validates the request and joins, runs C finalizers, releases
tracked allocations, then acknowledges the request and exits startup.

Join admission rejection retains the worker and returns
`KNYTOS_WAIT_REJECTED`. C must hold its borrow and observe the same worker
again or terminate the domain. C exit closes start admission before
finalizers. A return to exit with any unjoined worker aborts the domain before
finalizers or allocation cleanup. Worker callbacks return to the Rust
trampoline, which performs the thread exit.

The same profile exposes opaque C mutex and condition objects. Constructors
return null on allocation failure. Destroy them through the corresponding
destroy call after excluding concurrent access. Destruction returns
`KNYTOS_BUSY` while the object still has a lock owner or waiter. The runtime
tracks these as local heap metadata. They contain no kernel handles.

Mutexes are nonrecursive. Try-lock returns `KNYTOS_BUSY` for contention.
Timed lock returns `KNYTOS_DEADLINE` if the mutex remains held at the deadline.
Unlock verifies the runtime participant generation, so a replacement worker
in the same slot cannot unlock its predecessor's mutex. A callback must
release its mutexes before it returns.

A condition wait takes a mutex currently owned by the caller. It registers
the waiter before releasing the mutex. A signal selects one pending waiter,
or all pending waiters when its `all` argument is nonzero. A signal with no
waiter leaves no token. Publish and recheck the application predicate under
the mutex even when the wait reports success.

Only `KNYTOS_OK` means the condition waiter reacquired the mutex. A deadline
or wait rejection after registration leaves the mutex released and removes
the waiter's registration. The timed API shares one timeout budget across
waiting and reacquisition. For a caller that provides a terminal path,
`knytos_condition_wait_event` waits for selection without a deadline and
starts its reacquisition timeout after selection. Keep both objects live
until the operation returns, including while reacquisition is pending.
`knytos_condition_wait_event_unbounded` also waits for mutex readiness
without a deadline. Its caller supplies selection, unlock and shutdown
progress for both phases. Unexpected wait rejection still removes waiter
interest and returns without claiming mutex ownership.

Read/write locks use the same object lifetime rules. Shared acquisition can
nest within a participant, with one unlock required for each acquisition.
An exclusive owner blocks all other participants. A queued writer blocks
new readers while allowing an existing reader to nest. Upgrading a shared
lock or acquiring again while holding it exclusively returns
`KNYTOS_INVALID`. The lock allows competing writers to race for ownership
after a wake. It provides no FIFO order.

`knytos_rwlock_try_write` checks exclusive acquisition without waiting.
`knytos_rwlock_read_event` and `knytos_rwlock_write_event` wait for unlock
readiness without deadlines, using the same state guard and waiter
withdrawal as timed callers. Retain the lock and supply unlock and shutdown
progress throughout acquisition. `knytos_rwlock_storage_size` reports the
opaque object's requested heap bytes, excluding allocator overhead.

The finite read and write calls carry one monotonic budget through every
recheck. If the last queued writer times out or its wait is rejected, the
runtime withdraws that registration and wakes any readers that are now
eligible, even while an earlier reader is still active. The try-read call
returns `KNYTOS_BUSY` immediately when it would have to wait. A lock stays
live through all calls and can be destroyed only after every owner and
waiter has left and the caller has excluded further access.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --native-workers --smp 2 --output target/c-runtime/worker-validation
```

The unjoined-worker control requires the runtime's unjoined-owner diagnostic
and the same domain's kernel-abort diagnostic, with no ordinary C exit.
Concurrent console writes can interleave, so its fixture print is not an
acceptance marker. These diagnostics do not establish physical retirement.

## Picolibc profile for worker calls

`build.py --worker-libc` builds a separate Picolibc archive with stdio
locking and the runtime's participant-local errno hook. It includes the
worker APIs. Errno storage belongs to one live participant, starts at zero
for each new worker generation, and stays valid only for that
participant's lifetime. Pointers returned by the errno hook stay within
their owning callback.

The bridge installs locks on stdin and on the shared stdout and stderr
stream before C startup. Recursive acquisition is keyed to the exact
participant. Contention uses the same private CQ waits as the bounded
mutex. The Picolibc lock hook has no error return, so it aborts the
domain if acquisition rejects or exceeds its five-second budget.
The exit path checks that startup has joined all workers before it
enters C finalizers or frees C allocations.

The qualified worker calls cover allocation and free, errno, numeric
conversion, formatting into private buffers, and output to the fixed
streams. Dynamic stream locking and fixed-stream close abort explicitly.
Close the worker group by joining every callback and returning from the
startup thread. The configuration uses no ELF TLS. Stateful libc routines
outside the qualified set still need their own concurrency audit.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 4 --output target/c-runtime/worker-libc-validation
```
