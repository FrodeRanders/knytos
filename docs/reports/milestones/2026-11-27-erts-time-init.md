# Native ERTS time-initialization adapter

Observed 2026-11-27 on `feature/erlang-runtime`, based on `95f7233c` plus
the preceding UTC sample/system-clock work and the source hashes in the
[qualification manifest](../../plans/evidence/erts-time-init/summary.json).
OTP remains pinned to
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d` (29.1.2).

## Result and startup boundary

The standalone adapter supplies `sys_init_time`, `erts_late_sys_init_time`
and status-bearing `knytos_erts_init_time`. It acquires one authorized UTC
sample through the existing complete native query owner, validates local UTC
projection and absolute signed monotonic availability, prepares all metadata,
then publishes the sample before committing the caller's record. Every error
preserves that entire record. Failed publication leaves the prior clock
snapshot unchanged through the existing publisher's contract. No authority,
pending operation or caller pointer is retained in the metadata.

The statusless initializer emits a bounded diagnostic and aborts if preparation
fails, including when diagnostic output itself fails. All native query owners
have settled before C receives a result or attempts publication. This adds no
remote teardown or new cleanup family.

In pinned Unix `sys.c`, `sys_init` calls `erts_init_sys_time_sup` after thread
initialization. That common function calls `sys_init_time`; the time
supervisor's mutexes are created later in `erts_init_time_sup`. This identifies
the intended early acquisition boundary. It does not qualify the full native
startup call path or every enclosing context: native sys header/Make routing
and platform initialization remain integration work.

## Metadata and validity policy

The result reports available, uncorrected monotonic time in signed nanoseconds,
unlocked/nonextended reads, null POSIX clock IDs and native function names.
Resolution metadata describes initial conservative numeric granularity:
ceil nominal nanoseconds per counter tick, then ceil positive drift scaling
for system time, expressed as bounded Hertz. It is not UTC accuracy, NTP
uncertainty, refresh rate or scheduler delivery latency. A full runtime needs
a descriptive-metadata policy across subsequent granularity changes.

The generic timer-wheel unit is one millisecond. The standalone platform
header maps performance-counter reads to local high-resolution monotonic
nanoseconds with unit `1_000_000_000`; it supplies no CPU-time/accounting ticks.
`erts_late_sys_init_time` needs no wrap extender and installs no refresh
producer. It does not renew the sample anchor or validity window.

The explicit probe uses a five-second sample-age policy. The compile-time
policy must be present and nonzero; `build.py --clock-sample-age-ms` records
the chosen value. This finite qualification window is not a long-running
runtime policy. Refresh scheduling/notification ownership remains mandatory
integration work before an emulator can use the clock continuously.

## Qualification and retained captures

`stage_time.py` extracts the actual LP64 result record, initializer and two
entry-point declarations from pinned common `sys.h`. The
[generated contract](../../plans/evidence/erts-time-init/erts_time_contract.h)
is selected only by `KNYTOS_ERTS_TIME_CONTRACT_PROBE`; the normal source includes
the real `sys.h`. The [consumer](../../plans/evidence/erts-time-init/contract.c)
compares record size/alignment and all fourteen scalar field types/offsets
against an independent expected record from the pinned text. A wrong field
type and missing/zero sample-age policies reject. The native initializer links
into a validated static AArch64 ELF. This is not a complete sys-header build,
OTP Make selection or configured emulator.

Nine [host scenarios](../../plans/evidence/erts-time-init/host-validation.json)
pass under undefined-behavior sanitization. They cover actual metadata values,
quantization arithmetic at frequency/rate boundaries, local performance-counter
units, pointer/zero-age rejection, query/projection/publication errors with
unchanged output, absolute monotonic errors and signed bounds, and terminal
startup failure. The late hook makes no new query or publication. Terminal
tests use harness exit 42 and failed diagnostic output, rather than guest
fault injection.

All 20 signed cases and three deployment controls pass on two guest CPUs.
The fixture calls the actual initializer, checks its pinned record fields,
uses the local counter/system-time interfaces on startup and one worker,
retains the immutable record through exact join and calls the late hook.
Missing `time=call` preserves both metadata and cold clock state. The void
initializer aborts its domain without that grant, and a later survivor passes.
The [serial excerpts](../../plans/evidence/erts-time-init/serial-excerpts.txt)
retain:

```text
ERTS TIME INIT pinned metadata local performance counter and worker reads PASS
ERTS TIME INIT missing grant preserves metadata and cold clock PASS
[erts] clock initialization failed
[c-runtime] abort
```

The [first local cross-build](../../plans/evidence/erts-time-init/failed-include-build/summary.json)
failed because its generated contract header could not find the adapter
header. Adding the adapter directory to the explicit probe include path fixed
that compile failure; no guest ran from it. Its diagnostic excerpt is retained
with normalized paths. Full original compiler stdout and the pre-fix builder
were not archived.

The [initial passing profile](../../plans/evidence/erts-time-init/initial-passing/summary.json)
also passed 20 signed cases, before review added absolute monotonic signed-range
validation before publication. It remains separate from the final passing
repeat. Its earlier C source was recovered and verified byte-for-byte against
the original build-manifest hash. The two previously qualified clock objects
remain byte-identical within the new three-member archive.

SDK query submission/mapping/cancellation faults were not newly injected.
Broader startup lock, IRQ, signal and reentrant console contexts remain
unqualified. Exact joins and zero tracked C blocks do not prove physical root
retirement. Original [failed realtime captures](2026-11-27-c-realtime.md#failed-captures-and-admission-qualification)
remain separate, and the earlier [object-store extent incident](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved.

Native header/Make/configure selection remains open. The existing configure
probe is unchanged and still stops at its wall-clock-source gate; no POSIX
clock answers, CPU accounting or full-emulator startup success are fabricated.

| Artifact | SHA-256 |
| --- | --- |
| Three-member clock archive | `7ce10a790ddaf566196cac8838de880dbf916efc0ff64fa417a15bd21790b15c` |
| Interface link ELF | `3e6add89e3903fb65adcbd3a02749f78a7120d0350d632e8434f14ee1e9d7d86` |
| Startup fixture, signed | `dd27244c84704611ebffa59bab018d27c9831bd31c773e3c911bf27c106c5bd9` |
| Kernel | `3b0c09e12e36c10d8b747d9a4f92053ac9728f0c5b160bdb2609252dcc08efcc` |

The manifest includes reproduction commands, toolchains, source hashes and
fixture/descriptor identities. The
[checksums](../../plans/evidence/erts-time-init/checksums.json) cover the
version-controlled evidence.
