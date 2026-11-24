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
always returns `KNYTOS_UNAVAILABLE` without changing the output: this profile
does not connect to the authorized time service. Uptime is never Unix time.

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

Each participant has a private launch CQ. Slot zero denotes startup, and
its private queue is distinct from the application reactor on CQ zero.
Publish an application predicate with appropriate synchronization before
`knytos_thread_signal` and recheck it after `knytos_thread_wait`. A wake can
arrive early, coalesce, or survive slot reuse. It carries no payload or exit
authority. Private waits are finite and check lifecycle before and after
the wait. Lifecycle wakes presently target CQ zero, so a private waiter can
remain asleep until its deadline in the absence of a peer wake.

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
the waiter's registration. The same timeout budget covers waiting and
reacquisition. Keep both objects live until the operation returns, including
while reacquisition is pending. This is a bounded runtime C ABI. An adapter
for a library with different timeout or mutex ownership rules must account
for those differences.

Read/write locks use the same object lifetime rules. Shared acquisition can
nest within a participant, with one unlock required for each acquisition.
An exclusive owner blocks all other participants. A queued writer blocks
new readers while allowing an existing reader to nest. Upgrading a shared
lock or acquiring again while holding it exclusively returns
`KNYTOS_INVALID`. The lock allows competing writers to race for ownership
after a wake. It provides no FIFO order.

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
