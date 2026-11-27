# ERTS bounded native bootstrap qualification

Observed 2026-11-27 on `feature/erlang-runtime`, based on
`b0663a87306f9099c8fec49f551a959db784dec6` plus the source hashes in the
[evidence manifest](../../plans/evidence/erts-native-bootstrap/summary.json).
This qualifies the native thread-library entry points within their bounded
profile. M5 remains incomplete: no official ERTS emulator is built or booted.

## Contract

`ethr_init` copies creation hooks and reserves the native identity and
thread-event registrations before committing its early phase. The unpublished
identity registration stays in one pending owner. Event-key admission rejection
cancels it before returning, without publishing hooks or changing the phase.
Successful keys stay reserved for the domain lifetime.

`ethr_late_init` validates and copies allocator/main-thread options, then uses
the existing complete pending event owner to prepare the startup context and
cached event. Only success publishes immutable configuration and enables
creation. Allocation-prefix rejection frees through the captured allocator and
leaves the early reservation intact for unstarted retry. Reentrant startup
initialization and thread control are fenced during allocator callbacks.

Each accepted worker establishes its exact adapter identity and cached event
before invoking the child hook or publishing parent-handshake completion.
Worker event setup failure takes terminal whole-domain abort while retaining
the exact worker record and prepared callback borrow. It does not return a
partial creation or run parent cleanup against uncertain child initialization.
This terminal policy does not provide recoverable per-worker setup admission.

Exit handlers can borrow the cached event before native TSD teardown. Normal
return, explicit exit and detached drain retain their existing exact-join
contract. Startup excludes producers, drains workers and explicitly clears its
own event. Neither logical event release, join nor an abort diagnostic proves
physical root retirement.

Native atomics and mutex-library setup are stateless. CPU-count optimization
hints are unused by native parking; no CPU discovery or successful configure
answer is fabricated. The independent private adapter initializers remain
available without enabling automatic worker events. They cannot replace keys
or hooks already reserved by integrated initialization.

## Qualification

All 21 bootstrap cases and 28 separate header-regression cases passed, with
three deployment controls per guest. Both campaigns used the identical
nine-member archive. The interface check matched 294 public functions and
33 native callable names with 327 retained references, compiled normal,
inline and debug modes, and rejected all four mismatch/profile controls.

The signed bootstrap profile covers early key-admission rollback and retry,
phase/repeat/shutdown errors, both allocator failure prefixes, allocator callback
reentry, copied callbacks/options and startup event construction. Workers have
their events before child hooks and preserve event borrows through exit
handlers. Normal return, explicit exit and detached drain end with exact joins,
zero thread events and zero tracked C heap blocks. A separate worker allocation
failure control requires its diagnostic, runtime abort and the same domain's
kernel abort, with no ordinary exit, child/parent continuation or C finalizer.
Normal and debug consumers use OTP's selected header.

The separate header regression repeats independent event/allocator setup,
low-level locks and condition waiters, scalar and double-word atomics, native
operation inventories and data barriers. Its four linked-ELF instruction checks
cover default/debug barrier and double-word fixtures. Separate fresh guests
keep this qualification independent of the earlier combined-campaign store
failure; they do not explain or close that finding.

Commands, signed/unsigned fixture identities, complete source and archive
manifests, deployment controls and interface results are retained in the
[evidence manifest](../../plans/evidence/erts-native-bootstrap/summary.json).
The [serial excerpts](../../plans/evidence/erts-native-bootstrap/serial-excerpts.txt)
retain the relevant runtime observations, and
[interface excerpts](../../plans/evidence/erts-native-bootstrap/interface-excerpts.txt)
retain the pinned initialization declarations and static linkage.

## Artifact identities and remaining work

Both campaigns use two guest CPUs, QEMU 11.1.3, OTP 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d` and Picolibc 1.8.12 at
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`.

| Artifact | SHA-256 |
| --- | --- |
| Native adapter archive | `0ef39934fc1a745557a38ad46300e8b969bd9f50a01a2361b70b116d5bdc9fba` |
| Selected OTP `ethread.h` | `397b09c1c34825ebb5b29881ad9bfc0eac5e1aae0b758d8e240c6dec29007470` |
| Bootstrap guest kernel | `5fc8e42503e6bfb63e10ca2d7c9feabdfae1976044126b8084536637fbbbf9ad` |
| Header-regression guest kernel | `81a5c733fe12801e746f3c1644c005e6605075908be9ddbaaf807075ad948e2e` |
| Firmware | `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d` |

The guests used separately built kernel and boot images; their hashes differ
and are recorded per campaign. Archive identity is shared across the two
campaigns; this record does not claim identical kernel binaries or attribute
their difference to a particular build input.

The profile still retains three worker records on LP 1 with default stacks.
ERTS CPU/runtime data, actual stack bounds, required native thread capacity,
thread-progress integration and the configure/build backend remain open. No
JIT, general POSIX layer or broader OTP compatibility is qualified here.

The [earlier object-store extent failure](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved. Its original capture and the distinction between observed
failure and source/bitmap inference remain separate from these passing guests.
