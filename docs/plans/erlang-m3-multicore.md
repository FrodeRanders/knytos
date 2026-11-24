# AtomVM scheduler work remaining after the M3 semantic tests

The native adapter runs one AtomVM scheduler on one Knytos thread. Varying the
guest CPU count qualifies the same interpreter and CQ path under different
kernel topologies. AtomVM SMP needs a separate runtime profile with the
following contracts. The restricted C worker profile now implements prepared
startup, owned join and private finite waits. See the
[worker contract](../../ports/libc/README.md#restricted-native-worker-profile)
and [native results](../reports/milestones/2026-11-24-c-native-workers.md).
The [synchronization results](../reports/milestones/2026-11-24-c-native-sync.md)
cover the bounded mutex and condition paths in that profile.
The [read/write lock results](../reports/milestones/2026-11-24-c-native-rwlock.md)
cover shared acquisition, writer admission and waiter withdrawal.
The [Picolibc worker profile](../reports/milestones/2026-11-24-c-worker-libc.md)
adds scoped errno and fixed-stream locking.

| Pinned AtomVM requirement | Knytos boundary and implementation needed |
| --- | --- |
| Scheduler entry with a `GlobalContext *` and a main/worker distinction | The worker profile prepares a bounded start record and distinguishes startup from workers with `GetTid`. It keeps a `ThreadHandle` through the exact-generation join. AtomVM can request scheduler creation from existing workers while holding scheduler locks. It needs a start policy compatible with that call path before enabling SMP. |
| Mutex, try-lock, condition variable and read/write lock | The worker ABI implements nonrecursive mutexes, read/write locking and finite condition waits. The AtomVM adapter still needs to map checked wait outcomes to its synchronization contract. Its condition wrapper must account for mutex reacquisition and the shutdown wake path. |
| Event poll interruption through `sys_signal` | Upstream elects one scheduler to poll. Keep the default CQ cursor with that poll role and transfer the role only after the previous holder has left the wait. Private condition waiters require a separate notification path, such as a private CQ with one consumer and a retained waiter generation. Sharing CQ zero would let a condition waiter consume the poller's wake. |
| Thread-safe C library | The separate Picolibc worker profile has participant errno and fixed-stream locking without ELF TLS. The AtomVM closure still includes mutable Picolibc environment and timezone state. Qualify or exclude stateful paths before enabling concurrent VM calls. Full ELF TLS needs template initialization, per-thread storage and user thread-pointer save and restore. |
| Process and VM teardown | Upstream decrements its scheduler count before the native entry returns. Retain the complete worker owner until `join_preserving` confirms the exact generation has exited. Continue to hold the VM context, stack-associated state and allocator custody on rejection. Stop and join every worker before destroying VM state and draining the C allocation list. |

Use the existing scheduler suite to confirm kernel thread behavior. It covers
cross-LP abort, migration, retirement and timer affinity, independently of
the C runtime. The M2 and M3 fixtures cover the sole consumer's wake-before-wait
case and timer deadlines. They leave the rotating poll role, concurrent
publication, private waiter saturation and shutdown wake fanout open.

Extend the qualified worker profile with the remaining synchronization
contracts. Exercise wait admission rejection, shutdown and thread generation
reuse before enabling the AtomVM SMP build. Measure 1, 2 and 4 AtomVM schedulers with
the same message and timer workload, including wake latency and idle CPU use.
Keep the signed thread and stack limits explicit for each run. See the
[runtime and threading contracts](../reference/contributor-contracts.md#threads-stacks-and-scheduler-handoff)
and [CQ wait contract](../reference/cq-wait.md).
