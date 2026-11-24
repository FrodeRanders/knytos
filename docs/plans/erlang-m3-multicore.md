# AtomVM scheduler work remaining after the M3 semantic tests

The native adapter runs one AtomVM scheduler on one Knytos thread. Varying the
guest CPU count qualifies the same interpreter and CQ path under different
kernel topologies. AtomVM SMP needs a separate runtime profile with the
following contracts.

| Pinned AtomVM requirement | Knytos boundary and implementation needed |
| --- | --- |
| Scheduler entry with a `GlobalContext *` and a main/worker distinction | The current entry syscall supplies an entry address and target LP. Prepare a bounded start record and publish its context before a worker can run. A comparison with the live startup thread's `GetTid` result can classify main versus worker. Retain the generation-bound `ThreadHandle` from spawn through confirmed exit. |
| Mutex, try-lock, condition variable and read/write lock | Review upstream `smp.h` and the scheduler's mutex/condition-variable protocol together. Condition wait must register a waiter while holding the mutex, release it before parking, and reacquire it before returning. Serialize readiness publication with the waiter state. Define lock order and the lifetime of each waiter record before introducing any worker. |
| Event poll interruption through `sys_signal` | Upstream elects one scheduler to poll. Keep the default CQ cursor with that poll role and transfer the role only after the previous holder has left the wait. Private condition waiters require a separate notification path, such as a private CQ with one consumer and a retained waiter generation. Sharing CQ zero would let a condition waiter consume the poller's wake. |
| Thread-safe C library | The current Picolibc profile has process-global errno and single-thread stdio. Qualify a thread-safe profile before concurrent C calls. Full ELF TLS additionally needs template initialization, per-thread storage and user thread-pointer save and restore. A thread-safe alternative needs to qualify the errno access and `sys/lock.h` hooks, stream locking and other mutable state reached by the final link closure. |
| Process and VM teardown | Upstream decrements its scheduler count before the native entry returns. Retain the complete worker owner until `join_preserving` confirms the exact generation has exited. Continue to hold the VM context, stack-associated state and allocator custody on rejection. Stop and join every worker before destroying VM state and draining the C allocation list. |

Use the existing scheduler suite to confirm kernel thread behavior. It covers
cross-LP abort, migration, retirement and timer affinity, independently of
the C runtime. The M2 and M3 fixtures cover the sole consumer's wake-before-wait
case and timer deadlines. They leave the rotating poll role, concurrent
publication, private waiter saturation and shutdown wake fanout open.

Qualify the new runtime profile in stages with one worker and a retained
handle, then recorded wait-before-signal and signal-before-wait interleavings.
Exercise lock contention, rejection, shutdown and thread generation reuse
before enabling the AtomVM SMP build. Measure 1, 2 and 4 AtomVM schedulers with
the same message and timer workload, including wake latency and idle CPU use.
Keep the signed thread and stack limits explicit for each run. See the
[runtime and threading contracts](../reference/contributor-contracts.md#threads-stacks-and-scheduler-handoff)
and [CQ wait contract](../reference/cq-wait.md).
