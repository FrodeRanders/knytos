# AtomVM scheduler wake order — 2026-11-25

Releasing the scheduler mutex before notifying the condition waiter
reduced mutex CQ wait calls per message by 25% with two schedulers and
32% with four. Four-scheduler throughput rose by 11.3% in the profiled
comparison. The one-scheduler control rose by 3.0%.

## Comparison

The [joint handoff profile](2026-11-25-atomvm-scheduler-handoff-overlap.md)
observed mutex CQ waits during condition reacquisition and election
misses on the same condition returns. This comparison changes the
normal handoff's wake order. Both variants publish poller availability
under the scheduler mutex. The candidate releases it before calling
the condition signal. The stop and join paths keep their existing
wake order.

This change permits a scheduler arriving in the unlock-to-signal
interval to observe the available poller role. It also allows existing
mutex waiters to compete for the scheduler mutex before the condition
notification. The condition retains its selected waiter bit and the
returning scheduler still rechecks the election under the mutex.
The notifying scheduler retains its VM context through the worker join.

The runs used the same signed four-CPU AArch64 QEMU TCG boot image,
firmware, service bundle and Erlang package. Both SMP variants carry
the same joint profiling probes. For each scheduler count the generated
scheduler C differs only in the two statements' order. The
single-scheduler control is the same signed ELF in both runs. CQ
wait counters count calls, including calls that observe a retained
generation. The normalized counts use messages from warmup, the
three timed trials and the load window.

## Results

| Schedulers | Messages/s, signal first | Messages/s, unlock first | Change | Reacquisition CQ calls per 1,000 messages, signal → unlock | Election misses per 1,000 messages, signal → unlock |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 146,405 | 150,762 | +3.0% | — | — |
| 2 | 10,787 | 11,028 | +2.2% | 121.3 → 91.5 | 118.4 → 90.0 |
| 4 | 6,422 | 7,150 | +11.3% | 608.7 → 415.8 | 483.0 → 454.8 |

The two-scheduler throughput change was within the shift of the
unchanged control. With four schedulers the total election miss rate
among condition returns rose from 51.2% to 52.4% as the number of
condition returns fell. Misses among returns with zero CQ wait calls
rose from 45.6% to 51.7%. Misses among returns with one or more calls
fell from 59.3% to 53.8%. Winning schedulers still found a process on
more than 99.9% of first scans at both scheduler counts.

| Schedulers | Reacquisition p95, signal → unlock | Timer late p95, signal → unlock | Message wake p95, signal → unlock |
| ---: | ---: | ---: | ---: |
| 1 | — | 2,854 → 2,972 µs | 102 → 68 µs |
| 2 | 64–128 → 64–128 µs | 3,648 → 2,856 µs | 418 → 392 µs |
| 4 | 256–512 → 128–256 µs | 3,216 → 3,364 µs | 462 → 330 µs |

Reacquisition p95 uses histogram buckets from samples taken at every
64th condition wait per participant. The Erlang timer and wake
percentiles each use 32 samples per configuration. All sampled idle
windows recorded zero completed CPU slice ticks.

## Validation

Both profiling campaigns passed 14 signed cases and three rejection
controls. Thirty-two host-side management drain, boot diagnostics,
boot result and CI-input checks passed.

The uninstrumented unlock-first build passed 14 more signed cases
and three rejection controls. Its four-scheduler fixture exercised
receive deadlines and joined all three workers. The busy and parked
VMs observed the signed shutdown at the nonblocking checkpoint and
the CQ zero wait, respectively. Both joined every worker, released
their C allocations and acknowledged the exact root generation. QMP
reported `guest-shutdown` and QEMU exited with status zero.

The [wake-order patch](../../plans/evidence/erlang-scheduler-wake-order/unlock-before-signal.patch)
recreates the change. Apply the
[joint probe patch](../../plans/evidence/erlang-scheduler-handoff-overlap/instrumentation.patch)
as well to reproduce the instrumented candidate. The
[run summary](../../plans/evidence/erlang-scheduler-wake-order/summary.json)
contains the raw per-slot counts, histograms and signed artifact
identities. The [serial excerpts](../../plans/evidence/erlang-scheduler-wake-order/serial-excerpts.txt)
retain the workload and teardown markers.
