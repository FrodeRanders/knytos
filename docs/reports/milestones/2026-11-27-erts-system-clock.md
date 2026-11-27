# Published ERTS system-clock prerequisite

Observed 2026-11-27 on `feature/erlang-runtime`, based on `95f7233c` plus
the caller-owned UTC sample work and source hashes in the
[qualification manifest](../../plans/evidence/erts-system-clock/summary.json).
The pinned OTP revision remains
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d` (29.1.2).

## Result and policy

The standalone clock archive now supplies `erts_os_system_time` and
`erts_os_times` alongside the two monotonic/high-resolution functions.
`knytos_erts_clock_refresh` performs the authorized blocking sample query
before attempting publication. `knytos_erts_clock_publish` accepts an already
prepared immutable sample, validates its current projection and signed range,
then attempts local publication. Neither function retries rejected work.
Validation does not authenticate caller-provided scalars.

Two static slots retain only copied sample/limit data, without capabilities,
heap storage or retained caller pointers. A reader increments a global count,
loads the active index, copies the selected slot and releases its pin before
projection. A serialized publisher requires zero pins before reusing the
inactive slot, writes its complete contents, then publishes the new index.
Readers arriving after the zero-count check can only select the prior active
slot or the completed replacement. Sequentially consistent count/index
operations order old-copy completion, reuse and new publication. The bounded
native roster has startup plus at most three workers and no reentrant read
callbacks, keeping the count far from wrap.

A reader does not acquire the publisher flag or return busy because a writer
is paused. Busy publication preserves the active sample. The read path has no
allocation, RPC, native mutex acquisition or explicit wait/retry; its native
atomic operations are lock-free, not a wait-free latency guarantee. Each read
projects its captured sample and enforces that sample's age, even if a newer
replacement has appeared. Publication retains the supplied counter anchor
rather than substituting publication time. Callers choose the explicit
nonzero nominal sample-age limit.

The status-bearing read preserves output on failure. The statusless ERTS
getter emits one bounded diagnostic and aborts on missing/expired time,
counter error or signed-range exhaustion. It releases its local pin before
that path and does not run C finalizers or worker cleanup. Failed diagnostic
output still terminates. Replacement may step UTC in either direction; no
global monotonic UTC clamp is added. Samples retain their original quality
metadata and supply neither an accuracy bound nor authenticated security time.

The paired getter follows OTP's generic fallback with sequential local
monotonic/system reads. It is not an atomic single-counter pair. No full
native sys header, `sys_init_time` metadata, ERTS startup hook, refresh
scheduling/notification owner, performance-counter selection or configure
selection is supplied. The native configure wall-clock gate remains open.
A runtime must initialize before first use and arrange refresh before its
chosen validity policy expires. IRQ, signal and reentrant console contexts
remain unqualified.

## Qualification

The [interface consumer](../../plans/evidence/erts-system-clock/contract.c)
extracts four function declarations and three scalar types from the pinned
OTP header and compares their actual types to the standalone header. The
complete seven-function archive surface links into a validated static AArch64
ELF. Return-type and paired-pointer mismatch controls both reject. The
[interface record](../../plans/evidence/erts-system-clock/interface.json)
includes symbols, compiler flags and source hashes. This is a link probe,
not an executed emulator or a complete native platform-header build.

Nine deterministic host scenarios pass under undefined-behavior sanitization.
They cover cold state, sequential pair reads, forward/backward replacement,
reader/writer busy admission, failed/successful explicit refresh, signed
boundaries, source statuses and terminal expiry. A paused publisher leaves
the prior sample readable. A separately paused reader releases its slot pin,
then completes from its private old copy after two publications reuse the
original slot. Terminal cases use harness exit 42 and failed diagnostic
output, rather than guest fault injection.

ThreadSanitizer passes both that deterministic reuse scenario and a stress
scenario with three readers performing 15,000 reads against 5,000 independent
publication attempts. Rejections are admitted outcomes, not readiness polling
or replay of accepted RPCs. The
[host validation record](../../plans/evidence/erts-system-clock/host-validation.json)
retains checked process statuses and sanitizer output.

All 20 signed cases and three deployment controls pass on two guest CPUs.
Startup acquires initial UTC outside native locks. One worker reads the
statusless getters under a native mutex while startup attempts 256 local
publications: 255 succeed and one rejects busy. Both participants perform
256 read iterations; the sequential pair is bracketed by monotonic reads.
After exact join and synchronization-object disposal, startup explicitly
refreshes again. Missing `time=call` preserves cold state, an uninitialized
getter aborts its domain, and a later survivor passes. The
[serial excerpts](../../plans/evidence/erts-system-clock/serial-excerpts.txt)
include:

```text
ERTS SYSTEM TIME published=255 busy=1
ERTS SYSTEM TIME local getters concurrent publication and exact join PASS
ERTS SYSTEM TIME missing grant preserves cold state PASS
[erts] system clock unavailable stale or out of range
[c-runtime] abort
```

Retained target disassembly shows the
[reader](../../plans/evidence/erts-system-clock/knytos_erts_clock_read.txt)
copies the full slot and decrements its pin before calling local projection.
The [publisher](../../plans/evidence/erts-system-clock/knytos_erts_clock_publish.txt)
validates before its admission flag and makes no calls while publishing.
[Refresh](../../plans/evidence/erts-system-clock/knytos_erts_clock_refresh.txt)
calls sample acquisition before publication. The final interface build and
guest fixtures use the same archive bytes. Rebuilt monotonic-only and default
ethread archives remain byte-identical to their previous qualifications.

Expiry/signed-bound terminal paths use deterministic host stubs; only cold
domain abort is newly guest-qualified. Query-specific submission, mapping and
cancellation faults were not newly injected and reuse the previously qualified
native query owner. Exact joins and zero tracked C blocks do not prove
physical root retirement. The original
[failed realtime captures](2026-11-27-c-realtime.md#failed-captures-and-admission-qualification)
remain separate, and the earlier
[object-store extent incident](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved.

| Artifact | SHA-256 |
| --- | --- |
| Two-member clock archive | `54f5b9f68066708e0a6a7625cb4d61e0bd2056dc6a262560afc0a3034e36e978` |
| Interface link ELF | `49538e16120e24971c560091f2bb4b8def9d529fbe5ab467cfc8b8ec1f055f71` |
| System-clock fixture, signed | `10d04a22fb77e1bbb62e8678ba8dc7c48bc5a20498c2d63d1372553f76cee0f0` |
| Kernel | `d6d6161e70706b1b1281cab752cc79506f6a05c7771869e72fbc79e81ac57397` |

The manifest records reproduction commands, sources, toolchains, every
passing case/control and fixture/descriptor identities. The
[checksums](../../plans/evidence/erts-system-clock/checksums.json) cover the
version-controlled evidence.
