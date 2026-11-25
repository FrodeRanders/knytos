# AtomVM native scheduler profile and remaining M3 work

The default adapter runs one AtomVM scheduler on one Knytos thread. Varying
the guest CPU count exercises that interpreter under different kernel
topologies. The [fixed native scheduler profile](../../ports/atomvm/README.md#fixed-native-schedulers)
opts into two or four AtomVM schedulers. The restricted C worker profile
supplies prepared startup, owned join and private waits. See the
[worker contract](../../ports/libc/README.md#restricted-native-worker-profile)
and [native results](../reports/milestones/2026-11-24-c-native-workers.md).
The [synchronization results](../reports/milestones/2026-11-24-c-native-sync.md)
cover the bounded mutex and condition paths in that profile.
The [read/write lock results](../reports/milestones/2026-11-24-c-native-rwlock.md)
cover shared acquisition, writer admission and waiter withdrawal.
The [Picolibc worker profile](../reports/milestones/2026-11-24-c-worker-libc.md)
adds scoped errno and fixed-stream locking.
The [native scheduler results](../reports/milestones/2026-11-24-atomvm-native-schedulers.md)
record the 1, 2 and 4 scheduler workload and the join of unused workers.
The [deployment retirement results](../reports/milestones/2026-11-24-atomvm-deployment-retirement.md)
cover an authenticated external request under runnable work and with parked workers.
The [cooperative retirement results](../reports/milestones/2026-11-24-atomvm-cooperative-retirement.md)
cover exact scheduler joins, local disposal and supervisor acknowledgement.
The [whole-node shutdown results](../reports/milestones/2026-11-25-atomvm-node-shutdown.md)
cover simultaneous busy and parked four-scheduler deployments through PSCI poweroff.
The [performance measurements](../reports/milestones/2026-11-25-atomvm-scheduler-performance.md)
cover message throughput, timer and wake latency, CPU utilization and memory.
The [CQ wake comparison](../reports/milestones/2026-11-25-atomvm-reactor-wakes.md)
repeats the message workload after coalescing elected-poller wake hints.
The [synchronization profile](../reports/milestones/2026-11-25-atomvm-scheduler-contention.md)
records private mutex and condition waits, scheduler signals and read/write
lock acquisitions on that workload.
The [handoff profile](../reports/milestones/2026-11-25-atomvm-scheduler-handoff.md)
records poller election outcomes, first-scan selections and sampled
condition and mutex durations.
The [joint election and wait profile](../reports/milestones/2026-11-25-atomvm-scheduler-handoff-overlap.md)
pairs each mutex reacquisition outcome with the ensuing poller election.
The [wake-order comparison](../reports/milestones/2026-11-25-atomvm-scheduler-wake-order.md)
measures the effect of releasing the scheduler mutex before the normal
condition notification.

| Pinned AtomVM requirement | Knytos boundary and remaining qualification |
| --- | --- |
| Scheduler entry with a `GlobalContext *` and a main/worker distinction | Startup prepares the fixed roster and retains each `ThreadHandle`. AtomVM reserves scheduler count under its own lock and releases a prepared worker after the ready-list publication. Dynamic reconfiguration returns `badarg`. |
| Mutex, try-lock, condition variable and read/write lock | The adapter uses the worker ABI. Conditions wait on the participant's private CQ and return to the VM with the mutex reacquired. Failed waits and lock calls abort. Contention and waiter-admission limits need targeted stress tests. |
| Event poll interruption through `sys_signal` | AtomVM elects one poller. The port retains VM wake hints during a runnable scan and posts a CQ generation for the first hint after arming. The C runtime guards the CQ zero cursor against overlap. Private waiters have their own cursors. The kernel independently wakes the poller for signed retirement. |
| Thread-safe C library | The Picolibc worker profile supplies participant errno and fixed streams. The remaining linked environment and timezone mutators sit behind the unavailable wall-clock call. Dynamic AVM package operations and the private-data NIF are excluded in the SMP lookup. The full concurrent NIF and libc surface needs additional auditing. |
| Process and VM teardown | The VM count can reach zero while native callbacks are returning. Startup wakes and joins every exact worker before freeing VM state. Join failure retains the worker owner through domain abort. The heap-only profile stops schedulers, completes C cleanup and acknowledges before startup exits. Signed deployment replacement and whole-node shutdown exercised this path. The agent retains each launch owner through reclamation. Cleanup for external NIF resources remains open. |

Use the existing scheduler suite to confirm kernel thread behavior. It covers
cross-LP abort, migration, retirement and timer affinity, independently of
the C runtime. The M2 and M3 fixtures cover the sole consumer's wake-before-wait
case and timer deadlines. Controlled wake injection during poller rotation
and private waiter saturation remain open.

The message-heavy workload still shows much lower throughput with SMP
on QEMU TCG. Private mutex and condition wait calls per message rise
with the scheduler count. Over 99.9% of winning condition handoffs
selected a process on the first scan, while 51% of four-scheduler
condition returns found another poller already elected. Releasing the
mutex before the normal notification reduced reacquisition CQ wait
calls per message. Contention remains when schedulers compete for
election after publication. Measure how often a newly arriving
scheduler claims the poller role before a notified waiter, and check
whether this pattern holds under sustained and compute-heavy workloads.
Separate allocator lock spins from C runtime metadata lock spins and
compare physical hardware before closing M3.
Exercise wait admission rejection while an SMP VM has
active and parked workers. Keep the signed thread and stack limits
explicit for each run. See the
[runtime and threading contracts](../reference/contributor-contracts.md#threads-stacks-and-scheduler-handoff)
and [CQ wait contract](../reference/cq-wait.md).
