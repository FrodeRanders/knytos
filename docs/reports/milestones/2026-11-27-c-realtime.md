# Authorized C realtime queries

Observed 2026-11-27 after committing and pushing
`1c0e8bd05506ed9deec3a6064ec14bdd9f369fba` on `feature/erlang-runtime`.
The following realtime changes are based on that commit plus the hashes in
the [qualification manifest](../../plans/evidence/c-realtime/summary.json).

## Result and contract

The service-worker C profile can now read actual UTC through its signed
`time=call` grant. `knytos_clock_realtime_ns` acquires an owned connection,
queries the time service and returns nanoseconds only after validating the
snapshot and disposing its returned mapping/memory owners. Ordinary and
worker-libc profiles still return unavailable. No POSIX clock function or
ERTS clock backend is introduced, and configure still stops at the wall-clock
requirement.

Each registered participant uses its private CQ. One monotonic one-second
budget covers grant acquisition and query observation; cancellation follows
the existing pending owner's checked/fail-closed policy. No capability or
pending operation crosses C, and no time sample or connection is cached
globally. This may block and is not an async-signal clock path.

Only canonical 80-byte synchronized snapshots produce a value. Invalid
schema/reserved bytes, inconsistent UTC fields, invalid nanoseconds and
unsigned timestamp overflow reject. Unsynchronized and holdover states remain
unavailable. Missing grants, deadline/wait errors and known unstarted profile
mapping admission retain distinct statuses; every error preserves output.
Synchronization does not authenticate upstream NTP or qualify security time.

## Failed captures and admission qualification

The [first run](../../plans/evidence/c-realtime/failed-worker-assertion/summary.json)
passed 16 baseline cases, then aborted at the worker result assertion. It did
not record the worker status. The
[diagnostic repeat](../../plans/evidence/c-realtime/failed-worker-diagnostic/summary.json)
also passed 16 baseline cases and recorded worker status 2 with its original
value 789 unchanged before the same assertion failed. Both aborted domains
are retained separately from the passing run. Fixture and pre-classification
Rust source snapshots were recovered and verified byte-for-byte against the
original build-manifest hashes.

Source inspection established that grant preparation maps the launch profile
and the kernel permits one mapping per object/address space. The C adapter
previously collapsed `MemoryStatus(ALREADY_MAPPED)` into unavailable. It now
reports that specific pre-submission rejection as `BUSY`. It does not retry,
wait by polling, or convert readiness into completion. Other failures retain
their original classes. The initial captures did not identify the exact
failure phase; they are not retroactively treated as per-phase diagnostics.

The passing run directly observed worker status 7 with unchanged output and
one unstarted profile-mapping rejection. After exact join it required a fresh,
uncontended worker query to succeed. Its extra signed thread headroom does not
assume immediate physical retirement of the first worker. Eight independent
startup attempts exercise response cleanup; rejected attempts are not hidden
replays of accepted work.

## Validation and remaining scope

All 19 signed cases and three deployment controls passed on two guest CPUs.
They include ordinary-profile clock unavailability, startup and worker UTC
queries, mapping-admission rejection, pointer checks, missing-grant denial,
exact joins, zero tracked C blocks and a later survivor. The
[serial excerpts](../../plans/evidence/c-realtime/serial-excerpts.txt) retain
those observations.

Seven host unit tests pass, including exact framing, invalid states/calendar
fields and integer boundaries. Run the host command recorded in the manifest
outside the workspace configuration search path, with the pinned toolchain.
Initial host invocations encountered freestanding `build-std`/core or panic
strategy conflicts before test execution; that terminal output was not
archived. The passing host output is retained separately.

Time-specific submission, returned-memory mapping and cancellation faults were
not newly injected; ownership composes the existing SDK policy. Zero C heap
blocks and joins do not prove physical root retirement. ERTS clock/backend
selection, wider thread support and the other M5 platform gaps remain open.
The earlier [object-store extent failure](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
also remains unresolved.

| Artifact | SHA-256 |
| --- | --- |
| UTC fixture, unsigned | `27529144169038e86dbdf85e02dd0fb14d303f5a90a4f45172c242cf61fa2b17` |
| UTC fixture, signed | `4589e8e17f78d5dcdb169241425805cfc3d6275b1f9e3e4f21be82d2bbf02886` |
| Kernel | `bcb7d9cc3570200f307c45776f887c950ed10ad6b13c1275239f8a487360a3d4` |
