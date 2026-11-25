# Historical reports

Everything below is a **point-in-time record**. Reports retain original
symptoms, findings, commit references, and validation evidence even after the
code changes. For current behavior, consult source/tests, the manual's status
appendix, and the living [`architecture/`](../architecture/README.md) and
[`reference/`](../reference/README.md) documents.

## Audits

The [audit archive](audits/README.md) indexes the dated reports and their retained
execution records. For current knowledge and work, use:

| Need | Document |
| --- | --- |
| Engineering lessons and failure patterns | [Security engineering](../guides/security-engineering.md) |
| Current dispositions, remaining gates and unresolved incidents | [Security status](../reference/security-remediation.md) |
| Ownership, cleanup and recovery boundaries | [Cleanup contracts](../reference/cleanup-recovery.md) |
| Detailed contributor requirements | [Subsystem contracts](../reference/contributor-contracts.md) |

## Investigations

- [AArch64 CI minimal boot timeout](investigations/2026-11-24-aarch64-minimal-timeout.md)
- [AArch64 CI firmware boot selection](investigations/2026-11-22-aarch64-uefi-boot.md)
- [Fusion ARM self-test fixture panics](investigations/2026-11-15-fusion-arm-selftest-panic.md)
- [Fusion ARM early-console and platform bring-up](investigations/2026-11-15-fusion-arm-console.md)
- [X86 kernel SysV/SSE2 qualification](investigations/2026-11-15-kernel-sse2.md)
- [X86 SysV/SSE2 runtime ABI 4 qualification](investigations/2026-11-15-x86-sysv-abi.md)
- [Freestanding x86 soft-float future-incompatibility warning](investigations/2026-11-15-x86-softfloat-future-incompatibility.md)
- [Upstream review before possible fork detachment](investigations/2026-11-05-upstream-review.md)
- [Native OTLP export and CPU pressure accounting](investigations/2026-10-31-native-otlp.md)
- [Minimal boot idle activity](investigations/2026-10-31-minimal-idle-profile.md)
- [Kafka soak scratch-window exhaustion](investigations/2026-09-14-kafka-soak-scratch-window-exhaustion.md)
- [DMA isolation and hostile-device security](investigations/2026-08-30-dma-security.md)
- [Frame-allocator interrupt-masking latency](investigations/2026-08-16-frame-allocator-irq-latency.md)
- [Live-upgrade stall](investigations/live-upgrade-stall.md)
- [Scheduler investigation](investigations/scheduler.md)
- [Intermittent AArch64 SMP context-switch panic](investigations/smp-context-switch-panic.md)

## Milestone records

- [AtomVM scheduler wake order](milestones/2026-11-25-atomvm-scheduler-wake-order.md)
- [AtomVM poller election and mutex wait overlap](milestones/2026-11-25-atomvm-scheduler-handoff-overlap.md)
- [AtomVM scheduler handoff profile](milestones/2026-11-25-atomvm-scheduler-handoff.md)
- [AtomVM scheduler contention](milestones/2026-11-25-atomvm-scheduler-contention.md)
- [AtomVM CQ wake comparison](milestones/2026-11-25-atomvm-reactor-wakes.md)
- [AtomVM scheduler performance](milestones/2026-11-25-atomvm-scheduler-performance.md)
- [AtomVM whole-node shutdown](milestones/2026-11-25-atomvm-node-shutdown.md)
- [AtomVM cooperative deployment retirement](milestones/2026-11-24-atomvm-cooperative-retirement.md)
- [AtomVM deployment retirement](milestones/2026-11-24-atomvm-deployment-retirement.md)
- [AtomVM fixed native schedulers](milestones/2026-11-24-atomvm-native-schedulers.md)
- [Picolibc worker profile](milestones/2026-11-24-c-worker-libc.md)
- [C read/write lock qualification](milestones/2026-11-24-c-native-rwlock.md)
- [C mutex and condition qualification](milestones/2026-11-24-c-native-sync.md)
- [Restricted C worker qualification](milestones/2026-11-24-c-native-workers.md)
- [AtomVM single-scheduler semantic qualification](milestones/2026-11-24-atomvm-semantics.md)
- [AtomVM M2 qualification](milestones/2026-11-24-atomvm.md)
- [Native C runtime M1 qualification](milestones/2026-11-24-c-runtime.md)

- [Async-syscall demonstration](milestones/async-syscall-demo.md)

When a report produces a durable invariant, copy that invariant into the
appropriate reference document and link back to the report as evidence.
