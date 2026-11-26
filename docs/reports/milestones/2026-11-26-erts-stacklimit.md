# ERTS stack-limit metadata — 2026-11-26

The [stack-limit adapter](../../../ports/erts/README.md#stack-limit-metadata)
implements the public OTP 29.1.2 getter and one-time setter. Startup and
each new worker generation begin with null metadata. A null installation
returns `EINVAL`. Once installed, every replacement, including null,
returns `EACCES`, matching upstream error precedence.

Values are borrowed pointers stored in the current participant's metadata.
The current worker uses its existing adapter identity, with no additional
TSD key or allocation. The value remains available during exit handlers
and TSD cleanup. Exact join or shutdown drain clears the retired record
before its next generation. Startup retains its separate value for the
domain lifetime. Native stack backing remains owned by the runtime and
kernel.

## Signed qualification

The two-CPU AArch64 QEMU synchronization campaign passed 29 signed cases
and three rejection controls. The archive-linked stack-limit fixture
checked initial null values, null rejection, one-time installation and
replacement-error precedence. Its child-initialization hook installed a
different marker for each worker, while startup retained its own marker.

Three workers remained live simultaneously. Their exit handlers and TSD
destructors observed their own installed values. Normal return, explicit
exit and detached shutdown drain preserved the metadata through cleanup.
A fourth worker reused the first adapter slot with a new generation and
observed null before installation. The shared marker storage remained live
through all exact joins and final drain. All synchronization objects were
disposed with zero remaining libc allocations.

The fixture uses address markers to test metadata identity. Those markers
represent no actual native stack boundary, and no overflow guard or stack
geometry was qualified. Existing timed native synchronization, mutex,
condition, read/write, archive-lock and creation-hook fixtures passed in
the same campaign. Native Rust sources were unchanged; this increment
ran the expanded synchronization campaign. Python syntax, Rust formatting
and diff checks passed.

The [summary](../../plans/evidence/erts-stacklimit/summary.json) records
archive, source, object-member and signed-fixture identities alongside
the pinned upstream interface and implementation hashes. The
[serial excerpts](../../plans/evidence/erts-stacklimit/serial-excerpts.txt)
retain the metadata and cleanup markers. The
[link-map excerpts](../../plans/evidence/erts-stacklimit/link-map-excerpts.txt)
identify the separately linked adapter objects used by the fixture.

## Remaining integration

Automatic capture of actual native stack boundaries remains open. The
adapter leaves unknown boundaries null. Its caller must supply the correct
boundary and retain the represented lifetime before ERTS runs code that
requires it. The pinned regular-expression stack guard asserts a non-null
limit. Sampling a stack pointer or stack high-water usage does not establish
the reserved mapping's edge.

The setter supplies metadata, while stack allocation, mapping and retirement
stay with the existing native owners. It establishes no mapping authority
or physical-release proof. Full OTP initialization, actual header selection,
allocator hooks, atomics, thread-specific events, thread progress and
coordinated shutdown still need integration. Existing explicit stack-size
options remain unsupported. The earlier
[object-store fragmentation finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved and is preserved separately from this fresh-guest run.

Repeat with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-mutex --smp 2 --output target/erts/stacklimit-validation
```
