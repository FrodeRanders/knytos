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

| Pinned AtomVM requirement | Knytos boundary and remaining qualification |
| --- | --- |
| Scheduler entry with a `GlobalContext *` and a main/worker distinction | Startup prepares the fixed roster and retains each `ThreadHandle`. AtomVM reserves scheduler count under its own lock and releases a prepared worker after the ready-list publication. Dynamic reconfiguration returns `badarg`. |
| Mutex, try-lock, condition variable and read/write lock | The adapter uses the worker ABI. Conditions wait on the participant's private CQ and return to the VM with the mutex reacquired. Failed waits and lock calls abort. Contention and waiter-admission limits need targeted stress tests. |
| Event poll interruption through `sys_signal` | AtomVM elects one poller. The C runtime guards its CQ zero cursor against overlap. Private waiters have their own cursors. Signed deployment retirement reached the CQ wait with parked workers and the nonblocking checkpoint when processes remained runnable. |
| Thread-safe C library | The Picolibc worker profile supplies participant errno and fixed streams. The remaining linked environment and timezone mutators sit behind the unavailable wall-clock call. Dynamic AVM package operations and the private-data NIF are excluded in the SMP lookup. The full concurrent NIF and libc surface needs additional auditing. |
| Process and VM teardown | The VM count can reach zero while native callbacks are returning. On normal return startup wakes and joins every exact worker before destroying VM objects or disposing of the C heap. Join failure aborts while retaining the worker owner. External deployment retirement aborts the original domain while the agent retains its launch owner. Cooperative VM disposal and whole-node shutdown with AtomVM present remain open. |

Use the existing scheduler suite to confirm kernel thread behavior. It covers
cross-LP abort, migration, retirement and timer affinity, independently of
the C runtime. The M2 and M3 fixtures cover the sole consumer's wake-before-wait
case and timer deadlines. Controlled wake injection during poller rotation
and private waiter saturation remain open.

Run the same message and timer workload across 1, 2 and 4 AtomVM
schedulers. Measure throughput, wake latency, idle CPU use and memory
before closing M3. Exercise wait admission rejection while an SMP VM has
active and parked workers. Keep the signed thread and stack
limits explicit for each run. See the
[runtime and threading contracts](../reference/contributor-contracts.md#threads-stacks-and-scheduler-handoff)
and [CQ wait contract](../reference/cq-wait.md).
