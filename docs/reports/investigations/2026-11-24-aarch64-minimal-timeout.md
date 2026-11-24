# AArch64 CI minimal boot timeout — 2026-11-24

The November 23 CI capture shows a kernel/service startup stall before bootstrap
seeding completes, with timers still active. Its cause remains unresolved. A
local rebuild and captured-kernel comparisons under QEMU 11.1.2 and 11.1.3
reach boot completion; passing runs do not explain the failed CI execution. No production behavior,
timeout or retry policy was changed during this investigation.

## Original failure

The supplied packet is
`boot-diagnostics-aarch64-unknown-none-knytos-minimal/aarch64-aic2zg3s`, from
clean revision `7d940ba0d42e1c694e5164032229f5dd96f357a6`, prepared on
November 23 at `13:05:11Z`. It booted a minimal release kernel on four CPUs
under QEMU 11.1.3. The kernel SHA-256 is
`08bc10d97d64fd80cf45d04c8b32b4b09cd22070cd404ca0c35840fb35ef66f5`.
All twelve entries in the packet's artifact digest manifest were verified.

The runner reached its original 180-second deadline at `180.087984167` seconds.
QMP and LLDB captured all four CPUs without reported errors. The
[retained serial stream](evidence/2026-11-24-aarch64-minimal-timeout/original-serial.txt)
ends at:

```text
[+     0.534144] [launch] driver grant batch prepared before threads: storage=true entropy=true network=true
[+     0.607790] [EL0 LOG] lp=0 asid=5 [rng] serving host-backed VirtIO entropy
```

Memory validation, all four timer initializations, SMMU initialization, trusted
admission publication and driver grant preparation preceded those lines.
There is no kernel panic, bootstrap-seed completion or `BOOT COMPLETE` event.
The firmware loaded the selected disk and entered Knytos. This is distinct
from the earlier [firmware-only timeout](2026-11-22-aarch64-uefi-boot.md);
the packet supplies no evidence of a common cause.

The [snapshot excerpt](evidence/2026-11-24-aarch64-minimal-timeout/original-snapshot-excerpt.txt)
shows:

| CPU | Captured execution |
| --- | --- |
| 0 | `sys_cq_wait_checked + 2084`, through `sync_dispatcher` and `sync_common` |
| 1–3 | `lp_idle_loop + 532` |

LP0's scheduler diagnostic identifies current TID 14, generation 16, ASID 6,
with dispatch counter 7,402. LP1–3 identify kernel idle threads, with dispatch
counters 2,212, 2,209 and 2,209 respectively. LP0 timer counters show 3,763 anonymous
events added, 3,760 fired, two cancelled and one queued, plus 3,598 keyed
events fired. The sampled timer counter is `11247727812` at 62.5 MHz,
approximately 180 seconds. These observations do not suggest a global early
boot freeze. A single snapshot does not establish that every blocked thread
had the correct notification source or that any particular I/O completed.

Source inspection places bootstrap seeding after driver-thread publication
and before cluster/appliance startup. Seeding can wait for object-store name
publication or an accepted object-store RPC. The packet lacks the parked
seeding thread's stack and the NVMe/object-store/name-service status pages.
The current CPU's CQ wait does not identify that dependency. Absence of NVMe
or NIC console output is also insufficient to conclude either driver failed:
minimal bundles suppress routine heartbeat/progress output.

## Local comparisons

All comparisons used four CPUs, minimal release mode, the normal protected
DMA/network configuration and the original 180-second observation window.
The first sandboxed attempt failed to bind QMP before guest execution; it is
separate from these guest runs.

1. The normal runner rebuilt services with
   `scripts/build-knytos-services.sh` and booted the current sources, whose
   executable source matches the failed revision. It passed minimal validation,
   emitted `BOOT COMPLETE` at `3.941826` seconds and obtained DHCP address
   `10.0.2.15/24`. Kernel SHA-256:
   `466e1802ca9fc815738f8d934957794527ba6970c071990aefa765079d1ad772`.
2. A separate boot image used the original captured CI kernel, byte for byte.
   All eight embedded bootstrap ELFs were extracted and verified against the
   original manifest, then included in a fresh local data image. This run also
   reached `BOOT COMPLETE` at `3.947735` seconds and obtained the same DHCP
   address. The retained summary records its final result and artifact hashes.
3. After upgrading local QEMU to 11.1.3, the captured kernel was booted again.
   Its kernel, boot image, initial data disk, complete service bundle and
   firmware hashes match comparison 2. The run passed the full 180-second
   window, emitted `BOOT COMPLETE` at `3.931429` seconds, and obtained DHCP
   address `10.0.2.15/24`. Its final QMP/LLDB snapshot captured all four CPUs
   without errors. The [separate serial capture](evidence/2026-11-24-aarch64-minimal-timeout/captured-kernel-qemu-11.1.3-serial.txt)
   and summary retain this result alongside the earlier run.

The initial comparisons used QEMU 11.1.2; comparison 3 matches CI's 11.1.3.
The AArch64 firmware is identical in all comparisons:
SHA-256 `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d`.
The original data disk and boot image were not supplied. Fifteen store-loaded
services in the captured-kernel comparison came from the local rebuild; only
the kernel and eight embedded bootstrap services match CI byte for byte.
The comparison is therefore not an identical-environment replay, and does
not isolate scheduling or disk contents as the cause. The failure also did
not reproduce after matching CI's QEMU version; version matching alone does
not establish an identical host environment or explain the original failure.

## Remaining evidence

On recurrence, capture the parked bootstrap-seeding thread's exact generation,
stack and accepted IPC call together with NVMe initialization detail, outstanding
command/CQE status, object-store stage/first I/O error, and name-service stage
and pending lookups. Preserve the original executable, initial disk identity,
QEMU version and firmware identity alongside that state. The existing host
capture contains CPU execution and scheduler/timer counters but does not
automatically collect these service pages or parked stacks.

The [retained evidence summary](evidence/2026-11-24-aarch64-minimal-timeout/summary.json)
contains the original manifest, capture outcome, artifact hashes, decoded
diagnostics and local comparison identities. Timeouts and readiness samples
remain observations; they authorize neither replay nor resource release.
