# ERTS fallback locks and standalone archive — 2026-11-26

The [low-level lock adapter](../../../ports/erts/README.md#low-level-lock-fallbacks)
provides OTP 29.1.2's four spinlock and six low-level read/write lock entry
points over the existing mutex and read/write adapters. These are blocking
userspace fallbacks following OTP's fallback model. Native spinlock feature
macros remain unset. The compiler-barrier entry point uses a C11 sequentially
consistent compiler fence.

The [archive builder](../../../ports/erts/build.py) compiles all six adapter
translation units separately and packages them as `libknytos_ethread.a`.
It prepares the pinned worker-libc SDK and records archive, object-member,
SDK-control and source identities. Consumers use the aggregate
`knytos_ethread.h` header. The archive and header expose standalone
prerequisites; OTP's actual thread-library header selection and configure
gate remain open.

## Signed qualification

The two-CPU AArch64 QEMU synchronization campaign passed 25 signed cases
and three rejection controls. The new fixture used only public headers
and the archive. Its positive link map included code from all six adapter
objects, covering event, TSD, thread, mutex, read/write mutex and low-level
lock implementations across translation-unit boundaries.

The fixture checked blocking spin contention and protected payload
publication, simultaneous readers, a writer waiting for read release,
and a reader observing payload after exclusive release. Bounded negative
observations confirmed absence of completion while startup retained
conflicting ownership. Busy destruction preserved both lock types, and
successful destruction and double-destroy rejection were checked.

Startup retained a keyed TSD value while the first worker observed an
initially empty value and installed its own borrowed pointer. It retained
the shared input through all four exact joins, deleted its TSD key and
disposed every synchronization object with zero remaining libc
allocations. The foreign-unlock fixture emitted the expected underlying
mutex error and C abort. A later signed C application completed successfully.

Existing timed mutex/condition and read/write fixtures and the earlier
ERTS synchronization fixtures passed in the same campaign. Native Rust
sources were unchanged from the preceding
[read/write qualification](2026-11-26-erts-rwmutex-adapter.md), which records
its separate worker-libc regression and host tests. This increment ran
the expanded synchronization campaign. Python syntax, Rust formatting
and diff checks passed.

The [summary](../../plans/evidence/erts-lock-archive/summary.json) records
the archive members, checked build inputs, signed fixtures, link-map
identities and pinned upstream declarations. The
[serial excerpts](../../plans/evidence/erts-lock-archive/serial-excerpts.txt)
retain success and fatal-control diagnostics. Normalized
[link-map excerpts](../../plans/evidence/erts-lock-archive/link-map-excerpts.txt)
retain one linked section per archive member, with temporary directory
prefixes removed.

## Remaining integration

Callers must permit parking and provide unlock and terminal shutdown
progress before entering these fallbacks. They retain the underlying
objects through waits and exclude destruction until calls and exact joins
finish. Busy rejection and notifications do not release ownership.
The full ERTS backend still needs call-site qualification, actual header
selection, initialization, atomics, thread-specific events, thread progress
and coordinated termination. This campaign did not inject allocation or
waiter-admission failure and did not prove the instant of scheduler parking.
The compiler fence adds no independent hardware synchronization claim.

The earlier
[object-store fragmentation finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved. This campaign used a fresh guest and preserves that
failed capture separately from its passing result.

Repeat with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-mutex --smp 2 --output target/erts/lock-archive-validation
```
