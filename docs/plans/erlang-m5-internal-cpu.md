# M5 internal libraries and common CPU pre-initialization

Build both real three-member ERTS internal archives with generated upstream
Make rules. Execute unchanged common CPU pre-initialization before the native
thread/clock pre-init entry point. No native CPU count, topology, affinity or
quota provider exists yet: preserve upstream unsupported results and the
single-scheduler fallback instead of inventing a hardware count.

Qualified on 2026-11-28: 29 signed cases and three deployment controls pass
on two guest CPUs, along with both library consumers, four archive/helper
rejection controls and prerequisite-mode rejection. See the
[results and artifact identities](../reports/milestones/2026-11-28-erts-internal-cpu.md).

## Acceptance and boundaries

- Add explicit native internal-library mode after native pre-init.
- Stage the genuine internal-library Makefile, build regular and reentrant
  archives and verify their three unchanged source/member identities.
- Build the unchanged common CPU-topology object through emulator Make rules.
  Native target-specific function/data sections permit section GC of unused
  functions; no replacement common-runtime symbols or POSIX success stubs.
- Link and run the real CPU-info allocation/update/destruction and unsupported
  getters, plus common CPU pre-early initialization and native pre-init.
- Exercise both internal-library variants, actual common buffer formatting, separate
  worker contexts, exact join and output/owner accounting.
- Inject malloc rejection at the test link boundary for direct CPU-info and
  common CPU pre-early initialization. Failed preparation must own no block;
  stop before native pre-init or later topology initialization.
- Keep the common runtime's successful CPU-info object for the domain lifetime;
  C runtime exit releases it only after worker drain. Do not pretend a public
  common CPU-topology shutdown API exists.
- Reject missing archives and attempts to link the unported `select`-based
  sleep and descriptor stream formatting. Check prerequisite selection and
  retain artifact identities.

This executes only CPU pre-early initialization, not its later allocator/binding
or topology-lock initialization, common emulator `early_init`, a scheduler or
an ERTS emulator. Unknown CPU counts are distinct from the chosen one-scheduler
policy. Native CPU discovery, continuous refresh, stack/capacity, remaining
platform hooks, full dependency closure and matching host bootstrap remain open.
The full startup caller also needs a terminal policy for rejected CPU-info
allocation: the common pre-early outputs become `-EINVAL`, while the later
topology initializer requires a nonnull CPU-info object.
