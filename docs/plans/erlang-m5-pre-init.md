# M5 native platform pre-initialization

This step implements the actual `erts_sys_pre_init` platform entry point for
the initial unbound, headless native profile. It initializes the native thread
library and invokes unchanged common `erts_init_sys_time_sup`, which consumes
the native clock adapter's real time record. It does not implement the later
emulator startup hooks or run a complete ERTS emulator.

Qualified on 2026-11-28: 26 signed cases and three deployment controls pass
on two guest CPUs. Actual common-code linkage, eight archive/profile rejection
controls and prerequisite-mode rejection also pass. See the
[results and artifact identities](../reports/milestones/2026-11-28-erts-pre-init.md).

## Contract and scope

The entry point is startup-only and one-shot. Native ethread retains its
existing early/late split: pre-init reserves the identity/event keys; workers
cannot be created until late initialization has completed. A repeated call,
worker call or thread preparation error uses the qualified terminal ethread
diagnostic. Failure of authorized clock preparation after thread commit also
terminates the partial startup domain. There is no retry of that committed
prefix, and no manual cleanup ladder.

The Unix pre-init's signal-mask/CPU-affinity inheritance hooks are absent in
this explicitly unbound profile. Native CPU binding is not implemented or
claimed. Lock instrumentation and signal-function selection must reject at
compile time until their initialization contracts are ported. No descriptor
reservation, terminal setup or POSIX success stub is imported from Unix.

## Acceptance

- Require explicit `--native-pre-init` selection after native time/metadata.
- Build the new platform object through the actual generated emulator Makefile,
  keeping its archive separate from the four-member clock archive.
- Link the entry point with the genuine common time-supervisor object, native
  ethread and clock archives, with complete upstream headers.
- Execute successful early initialization and late/worker handoff; verify the
  common time unit, cold-to-published clock transition, default creation gate,
  current calendar reads, exact join and final local owner release.
- Execute repeat, key-pressure, missing-grant and worker-call terminal cases
  in separate signed domains, followed by a survivor.
- Reject archive omission, unsupported instrumentation/signal profiles and a
  missing prerequisite mode. Record identities, failures and remaining gates.

The common clock metadata initializer is executed. The later
`erts_init_time_sup` correction/timer initializer and common early emulator
startup routine remain outside this qualification. Full native startup,
capacity/stack/CPU topology, continuous refresh, I/O, target internal libraries
and matching host bootstrap remain open.
