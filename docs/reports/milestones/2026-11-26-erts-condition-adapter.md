# ERTS condition-variable adapter — 2026-11-26

The [condition adapter](../../../ports/erts/README.md#condition-variable-adapter)
implements the six OTP 29.1.2 `ethr_cond_*` entry points and option layout.
It uses native condition registration and a new fully event-only wait
that retains both objects through selection and mutex reacquisition.
Successful return owns the mutex. Existing finite waits retain their
original deadline policy.

Registration precedes application-mutex release under the existing native
lock order. Signal selects one pending waiter, broadcast selects all,
and neither stores a token when no waiter exists. Notifications follow
producer-guard release. POSIX-compliant signaling without the associated
mutex is supported. Callers still publish and recheck their predicate
under that mutex. Neither selection nor reacquisition promises FIFO order.

Busy destruction preserves the condition object. Successful destruction
clears the wrapper pointer. External lifetime synchronization excludes
destruction throughout both wait phases. Unexpected wait, signal and
broadcast errors enter the adapter's fatal domain-abort path.

## Signed qualification

The two-CPU AArch64 QEMU synchronization campaign passed 21 signed cases
and three rejection controls. The positive condition fixture created
three workers and confirmed registration through their shared mutex.
It rejected destruction while waiters remained pending, selected one
worker with signal, and observed exactly one completion before broadcast
released the other two workers. Broadcast occurred without holding the
associated mutex, using POSIX-compliant options.

Startup retained the mutex after selection and observed no worker
completion during a bounded ten-millisecond interval. After unlock,
each returning worker confirmed mutex ownership and updated protected
state. Startup retained the shared input until all three exact joins,
then disposed all synchronization objects with zero remaining libc
allocations. Initialization covered invalid spin options and both default
and explicit options. Successful disposal and double-destroy rejection
were checked.

The negative fixture called wait without owning the mutex and emitted
the expected adapter error followed by C abort. A later signed C application
completed successfully. Existing mutex fixtures also passed.

The separate worker-libc regression passed 32 signed cases and three
rejection controls, including timed condition waits and deadline-bound
mutex reacquisition. Host validation passed 77 unit tests and one
compile-fail documentation test, with two documentation tests ignored.
Rust formatting, Python syntax and diff checks passed.

The [focused summary](../../plans/evidence/erts-condition-adapter/focused/summary.json)
and [regression summary](../../plans/evidence/erts-condition-adapter/regression/summary.json)
record executable identities, checked build inputs, adapter source hashes,
pinned upstream declarations and host validation. Their serial excerpts
retain the [condition results and fatal control](../../plans/evidence/erts-condition-adapter/focused/serial-excerpts.txt)
and the [earlier synchronization and ERTS results](../../plans/evidence/erts-condition-adapter/regression/serial-excerpts.txt).

## Remaining integration

These are standalone adapters. Full ERTS type selection and linkage,
thread progress and coordinated backend shutdown remain open. The backend
must publish terminal predicates, select waiters, release held mutexes and
confirm exact joins before disposing storage. Native wait rejection after
registration removes waiter interest without claiming mutex ownership;
the adapter treats that rejection as terminal. This campaign did not
inject waiter-admission failure or allocation failure.

The bounded observation confirms absence of completion while startup holds
the mutex, without proving the exact instant of scheduler parking. The
fixture does not qualify fairness, broader backend termination or the
AtomVM lifecycle integration. The earlier combined campaign's
[object-store fragmentation failure](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved. The two profiles continue to run in separate fresh
guests, preserving that failure separately from passing campaigns.

Repeat both profiles with new output directories.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-mutex --smp 2 --output target/erts/condition-validation
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/condition-regression
```
