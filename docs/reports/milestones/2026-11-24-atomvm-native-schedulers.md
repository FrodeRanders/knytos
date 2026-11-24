# AtomVM fixed native schedulers — 2026-11-24

AtomVM 0.6.6 runs the same signed Erlang message and timer package with
one, two and four schedulers on a four-CPU AArch64 QEMU guest. The
two- and four-scheduler images use the Picolibc worker profile pushed
as `106061cbc7baed5ce8dcab61baf49fe1c77c2480`. See the
[build and runtime contract](../../../ports/atomvm/README.md#fixed-native-schedulers).

## Native scheduling

Startup prepares a fixed roster and retains each native thread handle.
AtomVM reserves another scheduler in its running count and publishes
runnable work before releasing a prepared worker. Each worker waits on
its own CQ while parked. AtomVM's elected poller can move between
startup and workers. The C runtime rejects overlapping waits on CQ zero.
Private condition waits register under the condition and mutex metadata
guards before releasing the mutex. An event-only condition phase uses
a bounded reacquisition after selection. If the adapter observes a
failed wait, lock or join, it aborts while retaining VM and worker state.

On normal return, startup wakes unused native workers and joins every
exact thread before destroying the VM or entering C exit. It retains
the `GlobalContext` across the interval between the upstream scheduler
count reaching zero and the last native callback returning.

The builder applies narrow adaptations to generated copies of the
pinned files. AtomVM dispatches prepared workers after releasing the
ready-list and scheduler locks. Changing the online scheduler count
returns `badarg`. The SMP NIF lookup excludes dynamic package add and
close, as well as the private-data NIF that updates pin metadata under a
shared lock. The executable embeds the authenticated package. The writable
Picolibc link-map entries are environment and timezone state. The pinned
localtime NIF calls the unavailable wall-clock hook before reaching
those entries. See the
[writable entries](../../plans/evidence/erlang-native-schedulers/picolibc-writable-symbols.txt).

## Results

The final campaign passed all 25 signed cases and all three rejection
controls. The first passing campaign covered 24 signed cases before
the separate parked-worker case was added.

| Case | Configured schedulers | Native workers entered / joined | Reactor waits | Worker waits | Deadlines |
| --- | ---: | ---: | ---: | ---: | ---: |
| Message and timer workload | 1 | 0 / 0 | 3 | 0 | 3 |
| Message and timer workload | 2 | 1 / 1 | 13 | 7 | 3 |
| Message and timer workload | 4 | 3 / 3 | 20 | 17 | 3 |
| Hello with parked workers | 4 | 0 / 3 | 1 | 0 | 1 |

The four-scheduler workload entered worker slots 1 through 3 on LPs 1
through 3 and finished its Erlang workload on a worker. Both SMP
workloads logged CQ zero waits from startup and workers. All four
images reported zero remaining C allocations. The
[artifact summary](../../plans/evidence/erlang-native-schedulers/summary.json)
identifies each signed ELF and the shared workload package. The
[serial excerpt](../../plans/evidence/erlang-native-schedulers/run-3-scheduler-serial.txt)
correlates observations with the launch ASID.

The standard four-CPU AArch64 debug suite with `--security-test`
passed all 19 kernel self-tests. Its scheduler test covered timer affinity,
migration and cross-LP retirement. The 89 host Rust tests, 15 boot-image
and CI-input checks, workspace formatting and target Clippy for both C
libc profiles passed. See the
[kernel results](../../plans/evidence/erlang-native-schedulers/kernel-selftests.txt).

## Reproduction and limits

The [runner commands](../../../ports/atomvm/README.md#fixed-native-schedulers)
use QEMU 11.1.3 with four guest CPUs and sign each image with a thread limit
equal to the configured scheduler count. Each descriptor supplies 64 stack
pages per thread. The combined campaign adds the existing `--worker-libc`
and four single-scheduler VM regression cases. The first host reference run
caught a missing `spawn/1` wrapper in the test package. The fixture uses
the pinned built-in `spawn_opt/2` and passes on the same reference
interpreter. The superseded campaign was stopped during host preparation
before any submission. Concurrent rebuilds also collided in Picolibc's
shared CMake scratch directory. Sequential rebuilds completed.

The workload checks finite timer expiry and 24 tagged Erlang replies
over two waves. It exercises cross-LP scheduler entry, reactor waits,
and VM teardown. Sustained load, timer accuracy, throughput, idle CPU
use and wake latency remain unmeasured. Lifecycle delivery while an SMP
VM is externally retired and waiter-admission exhaustion remain open.
See the [remaining work](../../plans/erlang-m3-multicore.md).
