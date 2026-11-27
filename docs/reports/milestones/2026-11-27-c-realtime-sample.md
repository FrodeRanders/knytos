# Caller-owned UTC samples and bounded local projection

Observed 2026-11-27 after committing and pushing
`95f7233c` on `feature/erlang-runtime`. This qualification covers that commit
plus the source hashes in the
[manifest](../../plans/evidence/c-realtime-sample/summary.json).

## Result and contract

The C ABI now separates acquisition of an authorized UTC sample from local
clock reads. `knytos_clock_realtime_sample` uses the existing signed `time=call`
query, private participant CQ, one-second deadline and complete Rust query
owner. It copies a 48-byte structure only after validating and disposing the
returned memory mapping/owner. The sample contains UTC nanoseconds, matching
monotonic ticks/frequency, drift, original NTP age and uncertainty, plus an
explicit zero reserved field. No capability, operation or retained pointer
crosses C. Ordinary and worker-libc profiles report unavailable without
modifying any sample byte.

Both sample acquisition and the existing scalar UTC query reject a
nonpositive drift-adjusted rate or a synchronized frame carrying the
never-synchronized age sentinel. The protocol wire grammar is unchanged.
The target-only protocol dependency now has a separate host test dependency;
host decoder tests no longer activate an unused normal-library dependency.

`knytos_clock_sample_project_ns` copies one immutable caller-owned sample,
reads the kernel's counter/frequency once and estimates UTC with checked
integer arithmetic. It performs no allocation, RPC, wait, refresh, retry or
shared publication. Its explicit age limit bounds nominal elapsed time since
the sample. Exceeding the limit returns deadline. Changed frequency or a
counter before the anchor returns unavailable. Invalid metadata and arithmetic
overflow remain distinct. All errors preserve the output scalar.

The projection uses the producer's two flooring steps: counter delta to
nominal nanoseconds, then positive drift-rate scaling. Re-anchoring a rounded
sample can differ from the original model by nanoseconds; later corrections
and the service's nonregression clamp can differ by more. This is an estimate,
not a fresh service observation or a claim of continued synchronization.
Original NTP age/uncertainty remain metadata, and the sample-age limit is
neither a bound on them nor a UTC accuracy guarantee. Samples supply no
authorization or authenticated security time.

ERTS still needs safe publication, refresh, adjustment and terminal expiry
policies before replacing its statusless system-time getter. No sample cache,
refresh thread, POSIX clock implementation or configure selection is added.
The configure wall-clock gate remains open. IRQ, signal and reentrant console
contexts remain unqualified.

## Qualification

All 21 signed cases and three deployment controls passed on two guest CPUs.
The profile retains the scalar-query/concurrent-admission regressions and
adds sample acquisition plus denial without `time=call`. Its ordinary-profile
clock case checks whole-sample preservation when acquisition is unavailable
and local projection rejection without changing output.

The sample fixture checks C size/alignment/offsets, null/unaligned pointers
and a UTC/counter anchor bracketed by native monotonic reads. It publishes
one immutable sample before starting a worker on LP 1. Startup and worker
each perform 256 local projections under a native mutex, checking a common
nondecreasing UTC value within a five-second nominal sample-age limit. The
blocking sample query occurs before either participant takes that mutex.
After exact join it releases synchronization objects, then checks expiry,
overflow, future counter, changed frequency and malformed metadata with
unchanged outputs. The later survivor passes. The
[serial excerpts](../../plans/evidence/c-realtime-sample/serial-excerpts.txt)
include:

```text
C REALTIME SAMPLE bounded local projection startup and worker locked reads PASS
C REALTIME SAMPLE missing signed grant preserves complete output PASS
```

Twelve [host tests](../../plans/evidence/c-realtime-sample/host-tests.txt)
pass without warnings. They cover the C layout, copied anchor/rate/quality
metadata, exact framing and state validation, positive/negative drift,
fractional counter conversion, inclusive age boundary, changed/reversed
counters, invalid metadata and nominal/scaled/UTC overflow without saturation.

The retained disassemblies for both the
[ordinary profile](../../plans/evidence/c-realtime-sample/c-clock-projection.txt)
and [service-worker profile](../../plans/evidence/c-realtime-sample/c-realtime-sample-projection.txt)
show exactly one `svc #0x4f` (monotonic query 79), one call to the compiler's
`__udivti3` arithmetic helper and no indirect calls. Source inspection and
these artifact-specific observations establish the tested local path's
separation from grant/RPC/CQ waiting.

Clock-specific submission, returned-memory mapping and cancellation faults
were not newly injected; the sample API reuses the existing complete query
owner and checked cleanup policy. Zero tracked C blocks and joins do not
prove physical root retirement. Both original
[failed realtime captures](2026-11-27-c-realtime.md#failed-captures-and-admission-qualification)
remain separate, and the earlier
[object-store extent incident](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved.

| Artifact | SHA-256 |
| --- | --- |
| Sample fixture, unsigned | `2685f0051496e7f583927ae1c377a183a9e7d1feddc212efa43db694e9a47a92` |
| Sample fixture, signed | `80b437e7ebc57522334a1e56c4888740b9bdcea36605029a9751eb961b2b7cd0` |
| Missing-grant fixture, signed | `68cffe6cbfb9b75ba5886e7cb8c611e1d53f70c5055f3b391353b8024a32b24e` |
| Kernel | `9dff443bcafbb751aec71d636795b8589bca745da9b28278bbfe97b3e759f9c6` |

The manifest records reproduction commands, toolchains, sources, descriptor
identities, code-generation observations and all passed cases/controls. The
[checksums](../../plans/evidence/c-realtime-sample/checksums.json) cover the
version-controlled evidence; host paths and trailing blank lines are normalized
as recorded there.
