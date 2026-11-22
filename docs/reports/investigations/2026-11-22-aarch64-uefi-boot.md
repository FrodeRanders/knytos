# AArch64 CI firmware boot selection — 2026-11-22

The `Image type X64 can't be loaded on AARCH64 UEFI system.` warning identifies
an incompatible EFI image in QEMU's default VirtIO network option ROM. It does
not show that the AArch64 job packaged an x86 Knytos kernel. The original CI
pre-kernel stall remains unexplained: its diagnostic capture was not uploaded,
and a passing local reproduction cannot establish its cause.

## Evidence and limits

[CI run 44964330748](https://github.com/FrodeRanders/knytos/actions/runs/44964330748),
revision `261436436011cc8767dafffacf26d9bd1219563d`, failed only its AArch64
minimal boot job. The other three boot configurations passed. The supplied
serial excerpt contains firmware diagnostics and the X64 warning, without a
Knytos banner. The referenced capture was `aarch64-x0m_gvck`. The run uploaded
no artifacts; unauthenticated REST access to the job log returned HTTP 403.
The original failure reason, CPU state and complete serial stream are therefore
unavailable to this investigation.

The unchanged runner's local minimal release reproduction passed with the same
warning. Its archived kernel is AArch64 ELF machine `183`, SHA-256
`7e820fbf3e6e058b435b75b91b7e9dd9636e68b7b1314ab650b44bce16c887e8`.
The actual boot image contained only `EFI/BOOT/BOOTAA64.EFI`, PE machine `0xaa64`.
The loader SHA-256 was
`11288bad5042853c8fbad27ddf65f9bc53675959a87e0e67908a173ce1571479`.
Its serial stream proceeded from the warning to Limine, the Knytos banner,
`BOOT COMPLETE: validation passed`, and DHCP assignment.

QEMU's [VirtIO PCI NIC source](https://github.com/qemu/qemu/blob/master/hw/virtio/virtio-net-pci.c)
sets `efi-virtio.rom` as its default ROM. Inspection of the local QEMU 11.1.2
ROM found a legacy image followed at byte `69120` by an EFI image with machine
`0x8664` (x86-64). This accounts for AArch64 firmware rejecting an X64 image
while probing the NIC. It does not establish why the failed CI guest stopped
making progress. The
[October 22 capture](../audits/2026-10-22-security-deployment-output.md)
already recorded a firmware-only timeout with the same warning, CPU0 PC
`0x5fdf74ac` and secondary PCs zero; that incident also remains unresolved.

## Changes

- Set `bootindex=1` on the AArch64 `virt` FAT boot disk, selecting the intended
  OS volume explicitly. The `sbsa-ref` USB boot path already had this priority.
  QEMU's [boot-index contract](https://www.qemu.org/docs/master/system/bootindex.html)
  otherwise leaves devices without an index at the lowest priority, in
  unspecified order.
- Set `romfile=` on the AArch64 VirtIO NIC to omit its unused PXE ROM. The
  network device, DMA protection and runtime service configuration stay enabled.
- Check EFI and kernel machine headers against the runner's requested
  architecture before packaging. Mixed or malformed inputs fail before QEMU.
  Real FAT-image tests extract both selected loaders and reject an unexpected
  opposite-architecture fallback file.
- Upload failed CI boot captures for seven days, with distinct architecture/mode
  names. Preserve QEMU/package versions and firmware SHA-256 values alongside
  per-run serial, CPU/debugger state, manifests and immutable kernel artifacts.
  Data disks and management client key directories are outside this upload.

The original CI timeouts and failure semantics remain unchanged. There is no
automatic retry or conversion of firmware-only output into a successful boot.

## Qualification

The changed minimal release runner passed with four LPs, fresh storage and the
original 180-second bound. The X64 warning disappeared, the guest reached
`BOOT COMPLETE`, and DHCP assigned `10.0.2.15/24`. Its kernel SHA-256 is
`d8d04ef50fbe3f675f32d9b840bae8dc400c0f7fafc1a59ec2d26bd3c1ac639f`.
The first changed invocation was prevented from starting by the local sandbox's
QMP socket restriction; that host failure is recorded separately from the
unrestricted passing run.

The CI security configuration also passed with four LPs, fresh storage and the
original 180-second bound: `19 passed, 0 failed, 0 pending`, passed bitmap
`0x2003ffff`. It obtained a DHCP address and retired concurrent cancellation
traffic after `19768` requests. There was no X64 warning. Its kernel SHA-256 is
`aa6fd31a4fbc622fc0f71968e0530c878a57e76704d2de3af3b65cff3d94e338`.

All 44 focused host controls passed: boot payloads (4), real FAT images (5),
diagnostics (14), profiles (10), CI runtime inputs (6), and boot results (5).
Shell syntax, workflow YAML and diff whitespace checks passed. The architecture
guard also accepted the actual kernel/loader extracted from the existing
qualified x86 FAT image (kernel SHA-256
`6e6ea05671c4ac4b966d09cc760592b4cd5fe505e45b43c2c95e867b5d6ef4ae`).
That existing x86 image was inspected, not rebooted in this investigation.

Rebuilds regenerate fixture inputs. These runs qualify the selected boot paths;
they are neither a bit-identical replay nor a performance comparison.
The [retained evidence](evidence/2026-11-22-aarch64-uefi-boot.json) includes
commands, identities, firmware prefixes, ROM headers and verdicts. Local
temporary logs are not required to interpret it.
