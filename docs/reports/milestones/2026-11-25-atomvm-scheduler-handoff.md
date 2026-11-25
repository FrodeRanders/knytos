# AtomVM scheduler handoff profile — 2026-11-25

The first ready-list scan found a process after more than 99.9% of
successful poller elections from a condition return, at both scheduler
counts. Returns increasingly found another poller already elected as
the roster grew from two to four. Reacquiring the scheduler mutex also
took longer.

## Measurement

The signed run repeated the twelve-pair message workload on the same
four-CPU QEMU TCG boot, firmware and service bundle as the
[immediate-read run](2026-11-25-atomvm-scheduler-contention.md).
The one-scheduler control used the same signed ELF. The two- and
four-scheduler VMs used temporary probes in the generated scheduler
and the C worker runtime.

The scheduler probe counts returns from the condition wait used in
poller election. It checks whether the returning scheduler finds the
election available under the scheduler mutex. For each winner, it
records whether the first ready-list scan selects a process. A scan
with no selection can still service timers and events. The C runtime
probe counts mutex CQ wait calls during condition reacquisition, which
include calls that observe an already posted generation.

The C runtime samples every 64th condition wait per participant. The
first interval runs from condition entry until the waiter observes
selection. The second runs from selection until the application mutex
is reacquired. Both intervals include guest scheduling delays. The
histogram reports bounded ranges in guest monotonic microseconds.
Counts and samples cover the whole fixture, including quiet intervals
and teardown.

## Election and reacquisition

| Operation | 2 schedulers | 4 schedulers |
| --- | ---: | ---: |
| Condition returns through the election path | 144,535 | 372,726 |
| Returns finding a poller already elected | 47,852 (33.1%) | 190,252 (51.0%) |
| Returns winning election | 96,683 | 182,474 |
| Wins finding no process on the first scan | 65 (0.067%) | 67 (0.037%) |
| Condition mutex reacquisitions, including shutdown | 144,536 | 372,729 |
| Reacquisitions making at least one mutex CQ wait call | 42,841 (29.6%) | 152,019 (40.8%) |
| Total mutex CQ wait calls during reacquisition | 50,045 | 241,147 |

| Sampled interval | 2 schedulers p50 | 2 schedulers p95 | 4 schedulers p50 | 4 schedulers p95 |
| --- | ---: | ---: | ---: | ---: |
| Condition entry to observing selection | 64–128 µs | 256–512 µs | 128–256 µs | 512–1,024 µs |
| Observing selection to mutex reacquisition | 16–32 µs | 64–128 µs | 32–64 µs | 256–512 µs |

The ranges show histogram bucket edges. The two- and four-scheduler
runs collected 2,258 and 5,821 samples of each interval. The counts
and histograms describe the paths independently. Measuring how often
a contended reacquisition also misses the election needs a joint
sample.

The scheduler clears its poller flag before signaling the condition
variable. A scheduler entering that path can claim the role before
the signaled waiter reacquires the mutex. The observed elections
support investigating this competition. Any change to election must
preserve progress for runnable work, timers and deployment shutdown
across the handoff.

## Validation and scope

All 14 signed cases and three rejection controls passed. Every sampled
idle window recorded zero completed CPU slice ticks. Median throughput
was 10,840 messages/s with two schedulers and 6,460 with four, compared
with 10,982 and 6,432 in the prior uninstrumented run. The identical
single-scheduler control moved from 154,921 to 148,601 messages/s.
Condition activity also varied between the profiling runs. The
relative contributions of instrumentation and run-to-run variation
remain open.

The [instrumentation patch](../../plans/evidence/erlang-scheduler-handoff/instrumentation.patch)
recreates the temporary probes from `6ff819d5`. The
[run summary](../../plans/evidence/erlang-scheduler-handoff/summary.json)
contains the per-slot counts, histograms, benchmark results and signed
artifact identities. The [serial excerpts](../../plans/evidence/erlang-scheduler-handoff/serial-excerpts.txt)
retain the markers emitted after worker joins.
