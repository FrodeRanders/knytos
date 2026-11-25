# AtomVM scheduler performance — 2026-11-25

The performance fixture uses AtomVM 0.6.6 and runs the same embedded
Erlang package with one, two and four native schedulers. Each signed
descriptor allows the configured thread count and 64 stack pages per thread. The single
scheduler uses the default libc profile. The SMP images use worker libc.
The experiments use a Cortex-A710 guest under AArch64 QEMU 11.1.3 TCG
on a Darwin arm64 host. OTP 27.3.4.19 compiles the Erlang package.

## Measurement

Twelve pairs of Erlang processes exchange one request and one reply at
a time. After warmup, three trials each carry 96,000 messages. Throughput
uses the elapsed guest monotonic time for each trial, including batch
dispatch and collection. No host metrics requests run during the trials.

The fixture also records 32 ten-millisecond receive deadlines and 32
wake latencies. Timer error is elapsed time minus the requested duration.
The wake sample is the guest time from a send to execution of the
receiving Erlang process after a quiet interval. It includes mailbox
delivery and interpreter scheduling.

Two authenticated management snapshots fall inside each idle or loaded
window. The runner checks the guest counter boundaries and exact native
thread identities. It divides the sum of completed CPU slice ticks by
the observation interval times the configured scheduler count.
The idle endpoints also require each scheduler to be blocked. The
kernel memory counters record sampled heap and stack backing. See the
[workload and commands](../../../ports/atomvm/README.md#scheduler-performance).

## Four guest CPUs

| AtomVM schedulers | Median messages/s | Timer late p95 (µs) | Wake p95 (µs) | Idle CPU | Loaded CPU | Peak heap (bytes) | Sampled committed stack pages |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 150,001 | 2,574 | 140 | 0% | 91.59% | 40,100 | 1 |
| 2 | 9,106 | 3,424 | 520 | 0% | 83.33% | 43,520 | 2 |
| 4 | 5,540 | 3,252 | 736 | 0% | 63.50% | 43,409 | 4 |

The two- and four-scheduler builds delivered about 6% and 4% of
single-scheduler throughput on this workload. During the loaded sample
the one-scheduler VM had 305 native dispatches over 2.26 seconds. The
two-scheduler VM had 7,816 over 1.61 seconds and the four-scheduler VM
had 15,037 over 1.65 seconds. The observed
dispatch increase warrants profiling of scheduler handoffs, lock
contention, and TCG overhead before attributing the throughput cost.

Every scheduler stayed blocked at both idle sample endpoints and
accumulated zero completed CPU slice ticks between them. This evidence
covers the measured guest intervals. Long-duration stability, native
hardware scaling and unmeasured workload shapes remain open.

## Matching guest and scheduler counts

| Guest CPUs and schedulers | Median messages/s | Timer late p95 (µs) | Wake p95 (µs) | Idle CPU | Loaded CPU | Peak heap (bytes) | Sampled committed stack pages |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 156,352 | 2,822 | 78 | 0% | 91.79% | 40,100 | 1 |
| 2 | 9,034 | 3,192 | 514 | 0% | 84.35% | 43,409 | 2 |
| 4 | 5,540 | 3,252 | 736 | 0% | 63.50% | 43,409 | 4 |

The four-CPU row is the same run as above. The matched one- and
two-CPU guests reproduced the throughput drop and the zero idle
tick deltas. All five VM runs returned `ok` and reported zero
remaining C allocations. Each SMP run entered and joined its full
worker roster. The single-scheduler VM completed 13 load batches
in each run. The SMP VMs completed one long batch each, with
the CPU snapshot pair taken inside that batch.

The three guest campaigns passed 38 signed cases and nine build or
signature rejection controls. The fixture passed on the pinned
host reference interpreter. Thirty-two management, diagnostics,
boot result and CI-input checks passed. The
[measurements](../../plans/evidence/erlang-scheduler-performance/summary.json)
include each trial and latency sample, exact thread snapshot rows
and signed artifact hashes. The
[serial excerpts](../../plans/evidence/erlang-scheduler-performance/serial-excerpts.txt)
retain the workload and teardown markers.
