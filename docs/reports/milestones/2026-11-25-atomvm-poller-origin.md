# AtomVM poller election origin — 2026-11-25

With four schedulers, 95.3% of fresh poller elections occurred while
at least one selected condition waiter was still returning. Fresh
pollers were active at 74.7% of condition election misses. With two
schedulers, the corresponding figures were 50.5% and 100%.

## Measurement

The [wake-order comparison](2026-11-25-atomvm-scheduler-wake-order.md)
found fewer mutex CQ wait calls after moving normal condition
notification after the scheduler mutex unlock. This run measures
who takes the poller role on that variant.

A fresh arrival enters the scheduler election without returning
from a condition wait in that invocation. When it finds the role
available, the probe snapshots the scheduler condition's pending
and selected waiter sets while holding the scheduler mutex. A waiter
counts as selected from publication of its condition bit through
mutex reacquisition. This interval includes any delay between selection
and posting the CQ generation.

A separate counter records the active poller's origin whenever a
condition return finds the role occupied. The fresh-election snapshot
is aggregate across waiters at the decision. The miss counter describes
the active poller at the later condition return.

The signed repeat used the same four-CPU AArch64 QEMU TCG boot,
firmware, service bundle and twelve-pair Erlang workload as the
wake-order comparison. The one-scheduler control used the same
signed ELF.

## Results

| Waiter state at fresh election | 2 schedulers | 4 schedulers |
| --- | ---: | ---: |
| Neither pending nor selected | 151,438 | 486 |
| Pending only | 0 | 10,480 |
| Selected only | 154,662 | 101,833 |
| Pending and selected | 0 | 120,314 |
| Total fresh elections | 306,100 | 233,113 |
| At least one selected waiter | 50.5% | 95.3% |

| Active poller at a condition election miss | 2 schedulers | 4 schedulers |
| --- | ---: | ---: |
| Fresh arrival | 33,484 (100%) | 134,759 (74.7%) |
| Another condition return | 0 (0%) | 45,626 (25.3%) |
| Total misses | 33,484 | 180,385 |

All active pollers observed at a miss were classified. Condition
returns that won election selected a process on more than 99.9% of
first scans at both counts. A fresh poller can also finish before
a selected waiter returns. The two tables use separate denominators
and cannot establish whether a fresh poller displaced a particular
waiter.

## Validation and scope

The run passed 14 signed cases and three rejection controls. All idle
samples recorded zero completed CPU slice ticks. Median throughput was
11,159 and 7,120 messages/s with two and four schedulers, respectively,
compared with 11,028 and 7,150 in the prior wake-order profile. The
single-scheduler control moved from 150,762 to 150,567 messages/s.
The new probe adds condition metadata reads and updates, so the
counts describe the instrumented runs.

The [instrumentation patch](../../plans/evidence/erlang-poller-origin/instrumentation.patch)
recreates the probes from `25220ffa`. The
[run summary](../../plans/evidence/erlang-poller-origin/summary.json)
contains the raw counts, timings and signed artifact hashes. The
[serial excerpts](../../plans/evidence/erlang-poller-origin/serial-excerpts.txt)
retain the workload, counter and teardown markers.
