# Native C execution environment

The default C profile is single-threaded. The
[C application guide](../../ports/libc/README.md) also defines the opt-in
worker roster and worker libc profile. The
[M1 report](../reports/milestones/2026-11-24-c-runtime.md) records native qualification.
The [M2 AtomVM adapter](../../ports/atomvm/README.md) adds a single-scheduler
reactor over launch CQ zero; the [M2 report](../reports/milestones/2026-11-24-atomvm.md)
records signed execution and resource observations. AtomVM can also use the
[fixed native scheduler profile](../../ports/atomvm/README.md#fixed-native-schedulers).
General C resource tokens, service reactors and ELF TLS remain future requirements. The
[M0 discovery report](../plans/erlang-m0-discovery.md) records inspected sources,
executed probes, resource limits and outstanding qualification. The
[Erlang programme](../plans/erlang-runtime-port.md) supplies milestone gates.

## Scope and layering

The first supported profile is static AArch64 C11, one native thread,
no ELF TLS, no dynamic loader and no POSIX process/filesystem environment.
C applications remain ordinary authenticated EL0 domains under runtime ABI 4.
The existing kernel ELF loader, deployment descriptor, grant controller and
resource accounting remain authoritative.

```text
C application / native VM
        |
        +-- Picolibc string, math and console formatting subset
        |
        +-- versioned Knytos C API
                    |
                    +-- Rust startup, allocator and owner custody
                    +-- knytos-rt / knytos-app clients
                                  |
                                  +-- existing kernel ABI and services
```

The root-workspace `knytos-cabi` crate and `ports/libc` build tooling implement
the profile over Rust runtime ownership. AtomVM and later ERTS adapters can share this C API,
but their scheduler, package-loader and NIF implementations remain separate.

## Startup, image and termination

Keep one Rust-owned `_start`. It establishes the existing `Context`/allocator
and invokes the C application entry through an explicit C ABI. Startup may
construct bounded `argc`/`argv` from authenticated input; it does not synthesize
a global Unix environment. The C library must not install a second `_start`,
initialize an independent heap over the same arena, or clear mapped memory
using bare-metal RAM/stack linker symbols.

The generated linker script retains explicit constructor/finalizer ranges.
The C wrapper invokes constructors in ascending priority, then runs `main`
with one fixed program argument. Exit invokes `atexit` before reverse-priority
destructors. Empty linker ranges use zero-sized Rust boundary symbols, since
their addresses may coincide. Loader mapping already copies initialized LOAD bytes
and zeroes BSS, so do not overwrite the immutable launch/configuration pages.

Final output is a fixed-address little-endian ELF64 `ET_EXEC`, starting at the
reviewed application address, with separate page-aligned executable/read-only
and writable loads. The loader does not relocate a shared object or initialize
`PT_TLS`. Reject unsupported TLS/dynamic-link imports in build validation.
Use the existing signature note and signed descriptor; C API versioning does
not replace the kernel's exact runtime ABI requirement.

Returning from C `main` enters Rust-controlled cleanup and records the
application result. This must distinguish:

- Normal main return after all application owners are disposed.
- An individual worker's terminal exit through the worker trampoline.
- Authenticated cooperative shutdown after operations and workers are drained.
- Fatal application/runtime failure using the existing domain-abort policy.

`thread_exit` does not unwind C or Rust frames or join other threads.
`exit(status)` must not be implemented as a blind alias for that syscall.
The worker profile requires startup to join every exact thread before C
finalizers or heap disposal. Blocking or fallible remote teardown uses
explicit close. A destructor cannot manufacture successful cancellation.

## Allocator and libc profile

The default Picolibc 1.8.12 profile is cross-built with single-thread mode, TLS disabled and its
malloc implementation disabled. The Knytos C allocator facade uses the runtime
allocator, with no second `sbrk` heap over the Rust arena.

The C allocation ABI needs its own private layout metadata because `free(p)`
does not supply Rust's allocation layout. Retain requested size, actual layout
and alignment through `realloc` and release. Check arithmetic before allocation;
preserve the old block on realloc failure; define zero-size behavior and
`aligned_alloc` restrictions; free through the same allocator that created the
block. Do not allow a generic C free to destroy capability/mapping owners.

Picolibc's console `FILE` callbacks can feed bounded Knytos logging. An opaque
console object is not a global descriptor namespace. Include the required
string/conversion/formatting/math functions from the maintained library.
Unavailable file, signal and process interfaces either remain unresolved at
build time or fail through explicitly documented public operations. They may
not return successful dummy results.

Disabling TLS allows process-global libc state only because this profile has
exactly one native thread. It must not later be linked into an SMP VM with
global errno/reentrancy state. The libc and compiler-runtime closure must be
requalified before enabling threads. The worker libc profile uses
participant-local errno and locks its fixed console streams.

## C-facing ownership

Publish C-compatible fixed-width types, lengths, status codes and versioned
structures. Avoid Rust references, enum layouts and collections in the ABI.
Treat pointer validity as a caller precondition inside the same native trust
domain, with checked sizes/alignment at the FFI boundary; it is not a proof
that arbitrary C is memory-safe. Never unwind through C frames.

M1 exposes no resource tokens or asynchronous operations. Future resource
tokens must refer to entries holding actual `knytos_rt::owned` owners or
protocol owners such as `OwnedSocket`. Generation and kind checks reject stale
or mismatched tokens. A copied numeric token does not create another Rust
owner, and it must not expose a raw kernel capability for adoption.

Specify borrowed access, logical shared references and consuming transfer
separately. Use `call_move`, `send_move` and `reply_move` to transfer the owner;
on rejected submission preserve the original owner when the existing API
supports it. Borrow bootstrap resources through `Context`. `from_raw` remains
restricted to documented, exactly-once ABI adoption.

The initial C profile need not expose general asynchronous IPC. When added,
one native operation record must retain its pending call, mapped/borrowed
buffers, exact identities, deadline, authority, result attachments and all
delivery/cleanup reservations. Prepare those reservations before accepting
work. Timeout and wait-admission failure preserve the complete record. A CQ
notification is only a reason to observe the retained owner.

For a VM, the lifetime of an Erlang reference differs from the lifetime of the
native request. A resource finalizer/process-down callback relinquishes delivery
interest and marks the operation for already-admitted cleanup custody. It must
not allocate a cleanup worker or synchronously drop a blocking owner beneath
VM/GC locks. Only confirmed completion/cancellation authorizes physical release;
uncertain cleanup retains the owner under the existing fail-closed policy.
Dropping a reference does not undo remote effects or authorize replay.

The optional [AtomVM service worker](../../ports/atomvm/README.md#scoped-native-service-calls)
accepts one scalar job in a fixed C slot. The worker borrows the launch
grant controller and descriptor through `Context`. Rust retains the
acquired connection through terminal observation or cancellation of
its call and disposes of unused returned capabilities before publishing
the scalar result. The VM copies a value into its mailbox from CQ zero
readiness. Startup joins the worker before acknowledging shutdown or
releasing its local C graph.

## Clocks and events

Monotonic time is a checked conversion from the kernel counter/frequency.
Carry one absolute deadline through preparation, unrelated wakes and phase
changes. Overflow, zero frequency and unavailable clocks return explicit status.

UTC is provided by the authorized `time` service, not a realtime syscall.
Return unavailable/unsynchronized honestly. A VM hook that cannot return an
error, such as the selected AtomVM `sys_time`, needs a reviewed startup/caching
policy before it is enabled. Do not map uptime to Unix time. Cached extrapolated
UTC must retain its quality/age limitations and must not be used as a claim of
authenticated security time.

The future VM reactor must own one CQ consumer and bind all relevant pending calls
and endpoints to it. It drains observations, checks lifecycle, registers/rechecks
interest and waits with the earliest remaining monotonic deadline using
`knytos_rt::cq::wait`. Readiness, deadline and rejected waiter admission remain
distinct. Use the shared fail-closed policy unless a separately reviewed
owner-preserving admission-recovery contract exists. Do not duplicate Sitas's
ring consumer or poll replies using an unrelated queue.

The initial AtomVM adapter has no service calls or native workers. Its sole
reactor uses `knytos_event_wait` over `knytos_rt::cq::wait` on launch CQ zero,
with lifecycle checks before and after waiting. The C API distinguishes
readiness, deadline, rejected admission, invalid request and shutdown. The VM
retains absolute timer expiry across readiness; rejected waits abort instead
of restarting timers or polling. A self-posted CQ generation tests the
wake-before-wait path. This does not qualify future service-operation or SMP
notification ownership. The void upstream realtime hook explicitly aborts
with an unavailable diagnostic until an authorized UTC policy is implemented.

The fixed AtomVM roster borrows one private shard CQ per participant for
worker gates and condition waits. The elected poller alone waits on CQ zero,
which also receives lifecycle wakes. All private event waits need an explicit
terminal path. AtomVM assigns lifecycle handling to the elected reactor
before submitting workers and also observes it at a nonblocking scheduler
checkpoint. Private waiters keep their peer readiness contracts. Startup
waits for exact joins before disposing of the VM's local C allocation graph,
then acknowledges through the Rust lifecycle API. The deployment agent
retains the launch owner until kernel reclamation completes. See the
[signed retirement fixture](../../ports/atomvm/README.md#signed-deployment-retirement).

## TLS and future thread profile

The current loader has no TLS template/TCB initialization. The AArch64 context
does not establish or preserve a per-thread user `TPIDR_EL0` value. LP-base
helper definitions use that register, but their presence alone does not prove
active use. This requires a deliberate kernel/runtime ownership decision and
per-thread save/restore qualification before using compiler-generated TLS.

A future generic TLS design must cover all thread creation paths, initial
template copying/zeroing, alignment, TCB ABI, errno/libc data, context switching,
preemption/migration, cancellation and final thread retirement. Test initialized
and zero TLS in simultaneous threads before requalifying libc or either VM.
Do not use a single global TLS block or a fake successful `pthread_key` API.

Thread creation currently accepts an entry and LP, without a C argument or
caller-selected stack/TLS pointer. A C trampoline needs one prepared start
record retained across publication and failure, with a generation-bound
`ThreadHandle`. Thread join rejection must retain that exact handle.
Mutex/condvar/address-wait semantics are not supplied by naming a userspace
spin lock; establish a real wake producer and the readiness recheck.

## Acceptance boundaries

M1 acceptance requires native signed C execution and C↔Rust ABI tests,
constructor order, output/formatting, allocator failure paths, clocks and
normal/error termination. TLS use is an expected build rejection in this
profile. Native probes must measure ELF/package size, heap peak and stack
observations within the existing layout and signed limits. M1 reports sampled
stack depth as a lower bound; full stack high-water and VM stack budgeting
remain unqualified. No kernel layout or resource limit was increased.

M2 implements the pinned compatible Erlang compiler, AtomVM platform hooks and
authenticated module closure in the documented single-scheduler scope. M3 adds qualified synchronization/TLS/SMP where
required. M4 adds asynchronous service resources and bounded finalizer custody.
M5 requires an independently reviewed ERTS allocator, thread, image, I/O and
bootstrap design. None of these later features is implied by a successful
freestanding C link or a host VM build.
