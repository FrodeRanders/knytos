# AtomVM scheduler contention — 2026-11-25

The twelve-pair message workload recorded 826 private mutex and
condition CQ wait calls per 1,000 messages with two native schedulers
and 2,567 with four. Timed read/write locks entered the wait path only
23 and 27 times, respectively. These counts direct the next scheduler
investigation toward mutex acquisition, condition-variable handoff
and the runnable work found after handoff.

## Counter run

The signed repeat used the boot image, firmware, service bundle and
Erlang package from the [coalesced-wake campaign](2026-11-25-atomvm-reactor-wakes.md).
The one-scheduler control used the same signed ELF. The two SMP builds
counted synchronization operations in per-participant C runtime
counters and printed the rows after startup joined all workers.

Each SMP run carried 396,000 ping-pong messages across warmup, three
96,000-message timed trials and one load-window batch. The counters
span the full fixture, including timer and wake probes and teardown.
The CQ wait counts include calls that consumed an already posted
generation. The counters measure operation frequency. Time spent
blocked and time spent handling the calls in the kernel remain open.

| Operation | 2 schedulers | 4 schedulers |
| --- | ---: | ---: |
| Timed mutex acquisitions, including condition reacquisition | 915,701 | 1,162,027 |
| Timed mutex acquisitions finding the mutex free on the first probe | 737,782 | 778,536 |
| Mutex CQ wait calls | 196,569 | 552,266 |
| Mutex try-locks returning busy | 92,725 | 148,845 |
| Condition waits selected by a signal | 122,175 | 368,468 |
| Condition CQ wait calls | 130,689 | 464,293 |
| Condition signal calls | 396,295 | 396,297 |
| Timed read/write acquisitions | 396,694 | 396,695 |
| Read/write acquisitions succeeding on the first probe | 396,673 | 396,676 |
| Read/write CQ wait calls | 23 | 27 |

The pinned interpreter uses its condition variable for scheduler
election and shutdown. About 31% of signal calls selected a waiter
with two schedulers and 93% did so with four. The mutex counts include
the worker libc profile's locks. The kernel's `heap_lock_spins` field
also includes contended `CountingMutex` instances used by the C worker
runtime. Attributing that field to the allocator alone would overstate
the evidence for heap contention. [Runtime mutex accounting](../../../crates/knytos-rt/src/lib.rs)

The instrumented run delivered 10,725 messages/s with two schedulers
and 6,270 with four. The uninstrumented coalesced-wake run delivered
10,266 and 6,201. The unchanged one-scheduler control moved from
151,558 to 155,268 messages/s. This comparison records run-to-run
variation alongside the cost of the counter instrumentation.

## Immediate read acquisition

The timed read path constructs a deadline and samples its remaining
budget before the first lock probe. The VM takes roughly one read lock
per ping-pong message. The revised adapter first uses the C ABI's
existing nonblocking shared acquisition. A busy result enters the timed
read path with its existing deadline, writer admission and CQ wait policy.
Both entry points use the same locked admission check.

| Schedulers | Reference messages/s | Counter run messages/s | Immediate read messages/s | Reference wake p95 (µs) | Immediate read wake p95 (µs) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 151,558 | 155,268 | 154,921 | 66 | 84 |
| 2 | 10,266 | 10,725 | 10,982 | 772 | 322 |
| 4 | 6,201 | 6,270 | 6,432 | 852 | 344 |

The immediate read run reused the reference boot and service bundle.
It removed two clock queries from successful first-probe reads.
Throughput was 7.0% and 3.7% higher than the earlier uninstrumented
run with two and four schedulers, respectively. The identical
one-scheduler image ran 2.2% faster in that comparison. These
single-campaign results bound the observed change on this workload.
Four schedulers still dispatched about 9,790 times per second during
the loaded sample. All idle samples recorded blocked threads and zero
completed CPU slice ticks.

The immediate read campaign recorded a gap between boot validation at
4.1 seconds and the first C fixture launch at 144.8 seconds. That gap
preceded all timed workload trials. Its cause is open.

## Validation

The counter and immediate-read campaigns each passed 14 signed cases
and three rejection controls. A separate regression passed 21 signed
cases and three rejection controls. It exercised C mutex and
read/write lock contention, normal four-scheduler worker joins and
signed shutdown of busy and parked VMs. Both VMs joined all workers
and acknowledged their exact root generation before PSCI poweroff.
QMP reported guest shutdown and QEMU exited with status zero.
Thirty-two host-side management and boot checks passed.

The [counter patch](../../plans/evidence/erlang-scheduler-contention/instrumentation.patch)
recreates the temporary C runtime instrumentation. The
[read experiment patch](../../plans/evidence/erlang-scheduler-contention/read-probe.patch)
identifies the adapter change. The [run summary](../../plans/evidence/erlang-scheduler-contention/summary.json)
records the per-slot totals, timed samples and signed artifact hashes.
The [serial excerpts](../../plans/evidence/erlang-scheduler-contention/serial-excerpts.txt)
retain the workload, counter and teardown markers.
