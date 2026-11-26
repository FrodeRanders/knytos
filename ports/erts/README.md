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

Run the mutex and condition checks in their own signed campaign.

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
