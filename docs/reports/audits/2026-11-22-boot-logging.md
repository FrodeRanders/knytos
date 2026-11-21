# 2026-11-22: Non-minimal boot logging review

## Result

A full AArch64 test boot now emits **739 kernel lines** through its
successful registered-test verdict, compared with **1332** in the retained
pre-cleanup capture: **44.5% fewer lines** and **25.1% fewer normalized UTF-8 bytes**.
Both AArch64 captures finish with `passed=20 failed=0 pending=0` and the same
passed bitmap `0x303ffff`. The final x86 boot independently finishes with
`passed=16 failed=0 pending=0`, bitmap `0x3bbff`.

This is a console-noise comparison, not a boot-time or throughput benchmark.
The earlier AArch64 capture came from the
[broker qualification](2026-11-21-broker-async-dispatch.md); the new run uses the
ordinary non-minimal S3 fixture. The old fixture additionally prepared deployment
and shutdown ingress. Both use four-vCPU, 512 MiB AArch64 QEMU TCG guests and the
same registered test bitmap. Only the prefix through the first complete verdict
is compared; the later broker traffic and shutdown window is excluded. Firmware
and loader output is retained in the evidence but excluded from the kernel counts.
ANSI control sequences and line endings are normalized consistently.

## Value retained and noise removed

- Keep version/mode, CPU identity, memory/resource limits, hardware selection,
  trust publication, service readiness, actual state changes, named test results,
  intentional fault/quarantine evidence, warnings and failure context.
- Remove repeated initialization start/finish pairs, the allocator's construction
  transcript, temporary fixture ASIDs, successful thread-exit messages, deferred
  verifier/TID announcements, and repeated successful IPC-round progress. Keep
  the IPC episode-count summary and its exact-exit result.
- Remove the thread whose only work was printing the complete PCI topology.
  Immutable topology preparation still occurs before driver/test admission;
  selected devices and ECAM mappings remain visible. Unsupported ACPI entries
  now contribute to an ignored-entry count; invalid firmware still fails explicitly.
- Keep ordinary deferred-test progress as compact counts. Unresolved names and
  device phases are printed on a stalled heartbeat or terminal failure, rather
  than repeating the entire pending list on every successful advance.
- Default periodic service console heartbeats off for both test and minimal
  bundles, and for direct Cargo builds. `KNYTOS_SERVICE_HEARTBEATS=1` still enables
  diagnostic counters after rebuilding. Protocol heartbeats, operational status
  counters and service state/failure reporting remain active.

All assertions, registered tests, operation deadlines and cleanup paths remain.
Temporary address-space labels are now used in registration failures instead of
printing every successful allocation. The primitive virtual-memory probe uses
volatile write/read so removing intervening console calls cannot let the compiler
fold the probe into a constant success. Expected injected failures still print
because their retained-resource consequences are useful evidence.

Current policy and the heartbeat override are documented in
[boot modes](../../reference/boot-modes.md#compile-time-and-bundle-selection) and
[configuration](../../guides/configuration.md). Historical reports are unchanged;
this logging cleanup closes no security finding or hardware acceptance gate.

## Validation and identities

- Final non-minimal AArch64 `debug --tests --s3-test --fresh-storage --timeout 180`:
  all 20 registered tests passed, including NIC, DHCP and S3/TLS.
- Final non-minimal x86 `debug --tests --net-test --fresh-storage --timeout 300`:
  all 16 registered tests passed. Its redundant ISA messages and bitmap transcript
  were reviewed from a real run and trimmed before this final repeat.
- Strict kernel Clippy passed for both custom targets with `acpi,kernel_tests`;
  AArch64 service Clippy passed with denied warnings. Both native service bundles
  built through their normal scripts; workspace formatting passed.
- Boot-profile/heartbeat selection, boot-result parsing, CI runtime inputs and
  VMware ARM packaging host tests passed. Early development compile checks exposed
  unused print-only bindings and a direct S3 check lacking fixture-supplied CA
  input; the normal fixture build and final campaigns passed. No failed native
  boot was substituted for a passing qualification.

Both final builds use base `a666f31a4e9fc4120bb5933b50a45da70df67a4b` plus the
logging changes recorded by their manifests, pinned Sitas
`0b2a85c144f178dd85992e42889464937a498f74`, and nightly-2026-11-14.
The final guests were run concurrently, so their elapsed times are not compared.

| Capture | Kernel SHA-256 | Registered verdict |
| --- | --- | --- |
| AArch64 before | `babbc566e0a861ce8787ed67c4bc17926b411d8a880c3331f8d36af237d4b174` | 20 passed, 0 failed/pending |
| AArch64 after | `4a6e8221d175ceb9276e59d30b4bd5552d5231b804eb2e895e152918e250bd09` | 20 passed, 0 failed/pending |
| x86 after | `6e6ea05671c4ac4b966d09cc760592b4cd5fe505e45b43c2c95e867b5d6ef4ae` | 16 passed, 0 failed/pending |

[Retained evidence](evidence/2026-11-22-boot-logging/) contains the complete
normalized console prefixes, exact manifests, numerical summary and verdicts.
There is no matched pre-change x86 capture in this comparison. The earlier
AArch64 manifest identifies its source snapshot and boot-image inputs; the final
manifests retain the base revision, dirty paths/diff hash, service hashes, exact
kernel/boot-image identities and QEMU arguments.
