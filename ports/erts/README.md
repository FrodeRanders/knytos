# ERTS platform prerequisites

The port targets OTP 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`. The
[programme plan](../../docs/plans/erlang-runtime-port.md#12-milestone-m5--official-erts-port)
tracks the remaining threading, allocator, I/O and bootstrap work.

## Configure probe

`configure.py` exports the pinned OTP source into a new directory, adapts
target tuple recognition and runs its configure script against the
Knytos worker-libc SDK. See the [configure results](../../docs/reports/milestones/2026-11-26-erts-target-configure.md)
for arguments and the current thread-library rejection.

## Standalone archive

`build.py` compiles each adapter as a separate AArch64 object and packages
them as `libknytos_ethread.a`. It prepares the pinned worker-libc SDK,
uses the same freestanding C11 profile and emits an adjacent build manifest
with archive, member and source hashes. Use a fresh output path.

```sh
python3 ports/erts/build.py --picolibc-source /path/to/picolibc \
  --output target/erts/prerequisites/libknytos_ethread.a
```

Consumers include `knytos_ethread.h` and link the archive through
`ports/libc/build.py --include ports/erts --library /path/to/libknytos_ethread.a`.
The aggregate header exposes the standalone prerequisites. OTP's actual
`ethread.h` selection, initialization, double-word atomics and thread-specific events
still need integration. The configure thread-library gate remains open.

## Atomic operations

`ethr_atomics.h` provides the pinned interface's 32-bit and word-sized
atomic families for the AArch64 C11 profile. Each family includes init,
set, read, exchange, compare/exchange, arithmetic and bitwise operations
with the unspecified, dependency, read, write, acquire, release and full
barrier variants. The 184 entry points include the two address accessors.
These are inline functions compiled into their consumers. The archive
supplies the accompanying thread and event adapters.

Every operation variant currently uses sequentially consistent atomic
access and full fences before and after it. This deliberately exceeds the
weaker variants' minimum ordering requirements. Exclusive initialization
uses C11 `atomic_init` between those fences. Compile-time checks require
native lock-free integer and word operations. Double-word atomics and
upstream atomic-library initialization remain open.

Unsigned backing preserves modular arithmetic without signed overflow.
Compare/exchange returns the observed old value on either success or
failure. Bitwise read-and/read-or return the old value, while add-read,
increment-read and decrement-read return the new value. Storage remains
caller-owned and correctly aligned throughout all accesses. The borrowed
address accessor is for low-level interoperability; ordinary accesses
through it require exclusion from concurrent atomic users.

The signed fixture checks every operation/barrier combination, contended
arithmetic across four participants, and atomic publication before join.
Its publication flag has no CQ readiness source. A test-only observation
loop has a fixed load bound, one monotonic deadline and terminal failure;
production waiting still needs a concrete notification contract. The same
value matrix also runs under host undefined-behavior sanitization.
The [atomic qualification](../../docs/reports/milestones/2026-11-26-erts-atomics.md)
records the complete API inventory, signed results and concrete AArch64
publication fences.

## Native event adapter

`ethr_event.h` and `ethr_event.c` implement the event portion of the
pinned ERTS thread-library interface over the C native-worker event API.
The enclosing ERTS thread backend still needs to select this definition
and link the implementation.

Positive nanosecond timeouts round up to milliseconds. Zero checks
readiness immediately, and every negative timeout selects an indefinite
wait. An optional nonnegative spin count goes directly to the registered
native wait. Set and reset preserve the interface's memory barriers.
Successful waits return zero and timeout returns `ETIMEDOUT`.

Creation reports `ENOMEM` on allocation failure. Destruction returns
`EBUSY` for an admitted waiter and requires external lifetime
synchronization against new producer and waiter calls. Unexpected native
operation failures, including wait rejection and shutdown, abort the
domain. The enclosing backend must arrange a terminal set and join for
indefinite waiters during cooperative shutdown.

The standalone signed fixture compiles the adapter into its translation
unit and calls the ERTS event API. Run it through the ordinary worker
qualification with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/event-adapter-validation
```

## Thread-specific data adapter

`ethr_tsd.h` and `ethr_tsd.c` provide the pinned thread library's four
public keyed TSD functions. Keys use the native runtime's permanently
retired identities. Each participant's value remains borrowed through
its worker lifetime. Invalid or stale keys return `EINVAL` from set/delete
and null from get. Creation returns `EAGAIN` at the sixteen-key limit
and `EOVERFLOW` if the identity counter is exhausted.

The public key creation entry point supplies no destructor. The enclosing
backend can register its internal thread-event cleanup with
`knytos_tsd_key_create_with_destructor`. That callback runs with a cleared
key and a live participant identity before native exit. The standalone
TSD fixture combines both adapters and retains its shared argument through
join while the destructor releases the event and its containing allocation.

The full ERTS backend still needs to adopt these type definitions and
entry points and bind its internal event object to this cleanup contract.

## Bounded thread adapter

`ethr_thread.h` and `ethr_thread.c` provide startup-only create, join and
detach, self identity and identity comparison.
Three static start records retain each callback, borrowed argument and
returned pointer until exact native join succeeds. The adapter assigns
generation-qualified identities and rejects stale joins after slot reuse.
Join rejection retains the same record and native owner for observation.

The qualified options are joinable or detached threads with the default
stack and a name of up to fifteen bytes. Explicit stack suggestions return
`ENOTSUP`. A full roster returns `EAGAIN`. Callers retain argument data
and callback code through successful join. Startup uses identity zero,
and adapter-created workers retain their identities throughout TSD
destructor callbacks.
The adapter reserves one native TSD key for its domain lifetime.

This adapter uses native worker slots 1 through 3 on LP 1 and requires a
guest with at least two CPUs. Signed thread and stack admission remains
authoritative. Expanding the roster, background detached-thread reaping,
stack limits and thread-progress integration remain
backend work. The enclosing backend must also arrange terminal wakes and
join every worker during cooperative shutdown.

### Creation callbacks

Startup may call `knytos_ethr_threads_init` once, before the first create
attempt that reaches native submission. It copies the upstream
`ethr_init_data` callback layout. Null selects empty hooks, and omitting
this initializer also leaves the hooks empty. Registration returns
`EBUSY` after registration or creation has started, `EACCES` from a worker
and `ECANCELED` after the shutdown fence. This initializer covers the
standalone adapter; upstream `ethr_init` and `ethr_late_init` remain open.

Prepare runs on startup after local validation and record reservation.
Its returned pointer stays in that record through native submission.
The child establishes its adapter identity, invokes its captured child
callback and publishes completion before waking startup's private CQ.
Startup rechecks that completion after unrelated wakes and invokes parent
cleanup only after the child callback returns. A rejected native start
invokes parent cleanup without running the child callback. Preflight
rejection invokes neither callback. Parent cleanup may overlap user entry;
the child must stop using the prepared pointer before returning its hook.
Callbacks retain responsibility for their pointees and code lifetimes.

The record and exact worker owner remain retained through this handshake.
An unexpected handshake-wait error aborts the domain without returning an
unowned partial creation. Child initialization must return or abort; an
adapter thread-exit request from that callback aborts. Reentrant create,
join, detach, drain and callback registration from startup callbacks return
`EBUSY`. Callbacks run outside runtime guards. Shutdown still needs child
initialization progress before it can drain workers.
The [signed initialization qualification](../../docs/reports/milestones/2026-11-26-erts-thread-init.md)
records callback ordering, rejected-start cleanup, early wakes and the
fatal child-exit control, with the earlier thread suite repeated separately.

### Stack-limit metadata

`ethr_get_stacklimit` and `ethr_set_stacklimit` implement the pinned public
interface over participant-local borrowed metadata. Startup begins with a
null value, and each new adapter worker generation also begins with null.
Installing null returns `EINVAL`. After a non-null value is installed,
every replacement, including null, returns `EACCES`, preserving upstream
error precedence. A native worker outside the adapter has no stack-limit
slot; get returns null and set returns `EINVAL`.

Workers can install their value from the child-initialization hook. Their
value remains available through user entry, exit handlers and TSD cleanup.
Exact join or shutdown drain clears the retired record before slot reuse.
Startup retains its own value for the domain lifetime. These operations
allocate no storage and consume no additional TSD key. The caller retains
the represented stack boundary's lifetime; stack backing stays owned by
the native runtime and kernel.

Automatic discovery of native stack bounds remains open. The backend must
capture actual stack boundaries before running ERTS code that requires
non-null limits. A sampled stack pointer or sampled high-water mark does
not establish a reserved stack boundary. The signed metadata fixture uses
address markers and qualifies no native stack geometry or overflow guard.
The [signed stack-limit qualification](../../docs/reports/milestones/2026-11-26-erts-stacklimit.md)
records participant isolation, error precedence, cleanup lifetime and
fresh metadata after slot reuse.

### Thread names

Creation copies its optional name before native start and rejects names
longer than `ETHR_THR_NAME_MAX` (15 bytes) with `EINVAL`.
`ethr_setname` changes the calling thread's adapter name and ignores an
overlong input, matching the pinned interface. Null selects an empty name.
`ethr_getname` copies a consistent, null-terminated name for the supplied
generation-qualified identity. A small buffer returns `ERANGE`, an invalid
or retired identity returns `EINVAL`, and overlap with a rename returns
`EAGAIN`. Errors preserve the output buffer.

Name publication uses fixed atomic storage and one sequence check per
reader attempt. The calling thread is the sole writer while it is live.
Startup initializes the name before publication and clears its generation
after confirmed join or rejected native start. A retained thread's name
remains available during its TSD destructors. Startup has its own name
under identity zero.

### Detached custody and shutdown

Detached creation and `ethr_thr_detach` transfer result-observation interest
to the adapter. Public join rejects detached identities with `EINVAL`.
The native owner and its record remain retained until an explicit drain,
including after callback return and TSD cleanup notification.

Startup calls `knytos_ethr_threads_shutdown` after publishing terminal
predicates and wakes for every worker. The drain permanently fences new
creation, joins every retained worker and releases records after confirmed
exit. A rejected join retains that record and every remaining record for
a later drain call. Previous successful joins stay released. Repeating a
completed drain succeeds. Creation after the fence returns `ECANCELED`.

Retain callbacks, arguments and shared state through successful drain.
The drain settles both joinable and detached workers and discards their
return pointers. Callers remain responsible for any pointees they own.

### Explicit thread exit

`ethr_thr_exit(Result)` stores the worker's return pointer and enters the
native terminal exit path. TSD destructors still run with the participant's
identity and name available. A joinable worker returns its stored pointer
only after confirmed native join. Detached workers retain their record
until the shutdown drain confirms exit.

This entry point supports adapter-created workers. It aborts when called
from startup or recursively from TSD cleanup. The caller must leave no
Rust owner or guard requiring stack unwinding above the terminal C boundary.
Registered adapter exit handlers run before native TSD cleanup on both
normal return and explicit exit. Full ERTS still needs to install its own
callbacks and connect thread-progress cleanup before this boundary.

### Exit handlers

`ethr_install_exit_handler` admits up to sixteen callbacks registered by
startup. A null callback returns `EINVAL`, a full registry returns
`EAGAIN`, worker registration returns `EACCES`, and registration after
the shutdown fence returns `ECANCELED`.

Each adapter worker exit captures the published handler count and invokes
that snapshot in reverse registration order before TSD destructors. Entries
remain immutable after publication and callbacks run outside runtime
guards. Registration by startup while workers are live can affect a
later exit snapshot. Callback code must remain callable through every
retained worker's exit and drain.

An exit request from a running handler aborts the domain. A later recursive
request from TSD cleanup reaches the native recursive-exit guard. Handler
registration remains fixed storage for the domain lifetime.

## Mutex adapter

`ethr_mutex.h` and `ethr_mutex.c` implement the pinned ERTS mutex
interface over native nonrecursive mutexes. Default and optional
initialization return `ENOMEM` on allocation failure and reject spin
counts below -1 with `EINVAL`. Optional spinning goes directly to the
registered native wait. The backend supports the POSIX-compliant locking
convention.

`ethr_mutex_lock` uses event-only acquisition with no artificial deadline.
The owner must eventually unlock, and the enclosing backend must arrange
that progress during shutdown. Unexpected lock or unlock errors abort
the domain. Trylock returns `EBUSY` while held, including a same-thread
attempt, and `EINVAL` for an absent native object.

Destruction requires external lifetime synchronization. It returns
`EBUSY` while ownership or waiter metadata remains live, retaining the
object on rejection. Successful destruction clears the wrapper's pointer.
Callers keep the object live throughout all waits. Native wait rejection
removes only waiter interest before the adapter takes its fatal path.
The enclosing ERTS backend still needs to select these types and entry
points and qualify thread progress and shutdown.

Run the mutex, condition and read/write checks in their own signed campaign.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-mutex --smp 2 --output target/erts/mutex-validation
```

The mutex and worker-libc profiles use separate guests. A combined
34-case campaign encountered object-store extent exhaustion while the
catalog persisted another deployment, despite free disk space. The
[qualification record](../../docs/reports/milestones/2026-11-26-erts-mutex-adapter.md)
preserves that failure and the separate repeats.

## Condition-variable adapter

`ethr_mutex.h` and `ethr_mutex.c` also provide the six `ethr_cond_*`
entry points and the upstream option layout. Initialization returns
`ENOMEM` on allocation failure and rejects spin counts below -1 with
`EINVAL`. Optional spinning goes directly to the native registered wait.
The adapter supports the POSIX-compliant signaling convention, including
signal and broadcast without holding the associated mutex. Use matching
POSIX-compliant mutex and condition options when the caller needs that
convention.

Wait requires the caller to own the mutex nonrecursively. The native
operation registers condition interest before releasing the mutex, then
waits for selection and mutex reacquisition without deadlines. Successful
return always owns the mutex. Signal selects one pending waiter and
broadcast selects all. Neither retains a token in the absence of waiters.
Callers must publish and recheck predicates under their mutex. Wake order
and lock reacquisition have no FIFO guarantee.

Unexpected wait, signal or broadcast errors abort the domain. Busy
destruction returns `EBUSY` and retains the condition. Successful
destruction clears its wrapper pointer. External lifetime synchronization
keeps both objects live throughout selection and reacquisition and excludes
concurrent entry during destruction. The enclosing backend must arrange
terminal predicates, selection, unlock and exact joins before disposal.
Full ERTS linkage and coordinated shutdown remain open.
The [signed qualification](../../docs/reports/milestones/2026-11-26-erts-condition-adapter.md)
records the condition fixtures and the separate timed-worker regression.

## Read/write mutex adapter

`ethr_rwmutex.h` and `ethr_rwmutex.c` implement the eleven upstream
read/write mutex entry points. All supported type and lifetime options
use the same native algorithm. Reader groups are unused hints, matching
OTP's pthread backend. Spin counts go directly to registered waiting.
Invalid option enums or spin counts below -1 return `EINVAL`, and
allocation failure returns `ENOMEM`.

Readers can share and nest acquisition. A queued writer blocks new
readers, while an existing reader can nest. Each read acquisition requires
one matching release. Exclusive acquisition cannot nest or upgrade a read
lock. Try-lock reports held or incompatible ownership as `EBUSY`, and an
absent native object as `EINVAL`. Both blocking acquisitions wait for
events without deadlines. Unlock notifications follow guard release;
waiters recheck lock state rather than treating a wake as acquisition.
Selection and reacquisition have no FIFO guarantee.

`ethr_rwmutex_size` reports the native heap storage beyond the embedded
wrapper, excluding allocator overhead. Busy destruction preserves the
object, and successful destruction clears its pointer. External lifetime
synchronization keeps the object live through waits and excludes concurrent
entry during destruction. Unexpected blocking or unlock errors abort the
domain after native waiter-interest withdrawal. Callers pair read/write
acquisitions with their corresponding release and supply unlock and terminal
shutdown progress before entering an indefinite wait.

Full ERTS type selection, linkage, thread progress and shutdown remain open.
The [signed read/write qualification](../../docs/reports/milestones/2026-11-26-erts-rwmutex-adapter.md)
records shared readers, both blocked access modes, heap size and fatal
foreign release, with the timed worker suite repeated separately.

## Low-level lock fallbacks

`ethr_lock.h` and `ethr_lock.c` provide the four `ethr_spin*` entry points
over `ethr_mutex`, and the six low-level `ethr_rwlock`/read/write entry
points over `ethr_rwmutex`. This follows OTP's blocking fallback model.
The types use registered event-only acquisition with the same owner,
busy-destruction and error contracts as those underlying adapters.
The backend must permit parking and provide unlock and terminal shutdown
progress at each call site. These fallbacks advertise neither native
spinlocks nor native read/write spinlocks. `ethr_compiler_barrier` uses
a C11 sequentially consistent compiler fence.

The signed archive fixture includes only the public aggregate header,
links the separate objects, and retains its shared input through exact
joins. The `--erts-mutex` profile qualifies blocking spin contention,
shared readers, blocked readers and writers, busy destruction and fatal
foreign release through these entry points.
The [archive qualification](../../docs/reports/milestones/2026-11-26-erts-lock-archive.md)
records the signed fixtures, archive members and separate-object link map.
