# AtomVM poller election and mutex wait overlap — 2026-11-25

With four schedulers, 59.5% of condition returns that called CQ wait
during mutex reacquisition found another poller already elected.
The rate was 45.6% for returns that made zero mutex CQ wait calls.
The larger zero-call group accounted for 52.5% of all election misses.

## Measurement

The [preceding handoff profile](2026-11-25-atomvm-scheduler-handoff.md)
counted election outcomes and mutex reacquisition independently. This
repeat carries the mutex CQ wait result from each successful condition
return into that scheduler's poller election check. It counts four
outcomes using the same locked election predicate and the existing
condition and mutex implementation. The probe records whether the
condition return called CQ wait at least once during reacquisition.
A call can consume an already posted generation.

The signed repeat used the same four-CPU AArch64 QEMU TCG boot,
firmware, service bundle and twelve-pair Erlang workload as the
[immediate-read run](2026-11-25-atomvm-scheduler-contention.md).
The one-scheduler control used the same signed ELF. The counters cover
the whole fixture. The joint counts cover the ordinary election path.
The C runtime counts also include the startup scheduler's dedicated
shutdown wait path.

## Joint counts

| Schedulers | Mutex CQ calls during reacquisition | Won election | Another poller elected | Miss rate |
| ---: | --- | ---: | ---: | ---: |
| 2 | Zero | 71,060 | 37,329 | 34.4% |
| 2 | At least one | 26,780 | 14,710 | 35.5% |
| 4 | Zero | 119,985 | 100,569 | 45.6% |
| 4 | At least one | 61,960 | 91,035 | 59.5% |

For two schedulers the rate differs by about one percentage point
between the two groups. At four the difference widens to 13.9 points.
These are associations from the same condition returns. The probe
observes the election only after reacquisition. It leaves the effect
of waiting on election and the identity of the prior mutex owner
unresolved.

Winning schedulers found a process on more than 99.9% of first scans
at both counts. The C runtime totals matched the joint probe after
accounting for two four-scheduler returns through the dedicated
shutdown path. All election winners reached a first scan.

The normal producer clears poller ownership and signals the waiter
while it holds the scheduler mutex. Publishing availability under
that mutex and moving the signal after release is a focused experiment
for the extra reacquisition waits. A resulting speedup would need
qualification against ready work, timers and shutdown. Both election
groups should remain visible during the comparison.

## Validation

The run passed 14 signed cases and three rejection controls. The idle
snapshots recorded zero completed CPU slice ticks. Median throughput
was 10,690 and 6,411 messages/s with two and four schedulers,
respectively, compared with 10,982 and 6,432 in the uninstrumented
reference. The identical single-scheduler control moved from 154,921
to 149,542 messages/s.

The [instrumentation patch](../../plans/evidence/erlang-scheduler-handoff-overlap/instrumentation.patch)
recreates the probes from `369f84f0`. The
[run summary](../../plans/evidence/erlang-scheduler-handoff-overlap/summary.json)
records the joint counts, per-slot histograms and signed artifact
hashes. The [serial excerpts](../../plans/evidence/erlang-scheduler-handoff-overlap/serial-excerpts.txt)
retain the benchmark, probe and teardown markers.
