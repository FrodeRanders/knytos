# AtomVM elected-poller CQ wakes — 2026-11-25

AtomVM elects one native scheduler to check timers, runnable processes
and system events. The Knytos adapter now retains VM wake hints in
userspace while that poller scans. A retained hint causes one more
scan before the poller parks. The first signal after the poller arms
advances the kernel's CQ zero generation, including if the signal
arrives before the wait enters the kernel. Lifecycle requests still
wake CQ zero directly through the kernel. At each return to the
scheduler, AtomVM checks its own runnable state and absolute timers.

## Signed comparison

Both runs used the same four-CPU AArch64 QEMU 11.1.3 TCG configuration,
boot image, firmware and Erlang package. The original signed SMP
artifacts came from the [first performance campaign](2026-11-25-atomvm-scheduler-performance.md).
The runner reused the candidate's boot image and development TLS
enrollment for the baseline repeat. The one-scheduler control used the
same signed ELF in both runs. Each throughput entry is the median
of three 96,000-message trials from the same twelve-pair ping-pong
fixture. The p95 latency values use 32 samples per configuration.

| Schedulers | Baseline messages/s | Coalesced messages/s | Change | Timer late p95 (µs), baseline → coalesced | Wake p95 (µs), baseline → coalesced |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 151,858 | 151,558 | −0.2% | 2,834 → 2,914 | 66 → 66 |
| 2 | 9,184 | 10,266 | +11.8% | 2,666 → 3,326 | 440 → 772 |
| 4 | 5,589 | 6,201 | +11.0% | 3,166 → 5,368 | 508 → 852 |

All sampled idle threads were blocked at both snapshots, with zero
additional CPU ticks. Normal SMP exit reported 83 VM CQ wake calls
with two schedulers and 77 with four. These counts span the whole
fixture. The baseline forwarded each VM hint to the kernel. The call
counter was introduced with coalescing.

The two- and four-scheduler runs still delivered about 7% and 4% of
single-scheduler throughput. The four-scheduler loaded sample still
saw about 9,200 native dispatches per second, compared with roughly
130 for one scheduler. Both sampled SMP latency tails increased.
The comparison sampled one host and a single workload shape. Private
wait frequency, handoff costs and latency repeatability remain open.

## Lifecycle regression

The rebuilt two- and four-scheduler fixtures completed their message
and timer workloads and joined every worker. The signed whole-node
shutdown then ran with busy and parked four-scheduler VMs. The busy
VM observed the request at the nonblocking scheduler checkpoint. The
parked VM observed it from the CQ zero wait. Each VM joined all three
workers, released its tracked C allocations and reached its
root-generation acknowledgement before the deployment agent completed
its drain. QMP reported `guest-shutdown` and QEMU exited with status zero.

The two performance runs passed 14 signed cases and three rejection
controls each. The lifecycle regression passed 22 signed cases and
three rejection controls. All 32 host-side management drain, boot
diagnostics, boot result and CI-input checks passed. The
[run summary](../../plans/evidence/erlang-reactor-wakes/summary.json)
contains the raw samples, thread snapshots and artifact identities.
The [performance serial extract](../../plans/evidence/erlang-reactor-wakes/performance-serial.txt)
and [lifecycle serial extract](../../plans/evidence/erlang-reactor-wakes/regression-serial.txt)
retain the workload and teardown markers.
