# M5 fixed console streams and terminal CPU preparation

Add native fixed-stream formatting through the actual locked C runtime
stdout/stderr, and terminate rejected CPU pre-early initialization before
publishing any startup state. Descriptor output and descriptor-based sleep
remain unsupported. The scope is a native startup prefix, not emulator boot.

Qualified on 2026-11-28: fresh-guest core and focused cohorts pass 29 and 16
cases respectively, each with three deployment controls. Their union covers
all 34 fixtures. Both library variants and nonnative preservation/link controls
also pass. See the [results and artifact identities](../reports/milestones/2026-11-28-erts-console-startup.md).

## Acceptance

- Add explicit `--native-console` after native internal-library selection.
- Patch only native branches of common stream helpers and CPU preparation.
  Preserve nonnative source lines, including string literals, with `unifdef`
  selection and whitespace-normalized comparison against the pin.
- Keep whole formatted calls serialized through the fixed recursive stdio lock,
  including `erts_vprintf`. Preserve CRLF conversion and error return contracts.
  Reject unsupported FILE objects and oversized spans before I/O.
- Exercise both internal archives, actual stdout/stderr calls, va_list forms,
  concurrent whole records, recursive locking, exact joins and owner release.
- Inject character failure and a real short-write prefix without retry or
  rollback. Test exact translated CRLF bytes at the fixture link boundary.
- CPU preparation must run once on startup, allocate before publishing outputs
  or its owner, and abort on allocation failure/reentry/worker invocation.
- Inject CPU allocation and diagnostic-write failure; verify untouched outputs,
  zero prepared blocks and a terminal abort even when logging fails.
- Keep successful runtime CPU owners through worker drain and normal domain
  exit. Reuse existing C allocator/domain-abort contracts; add no cleanup family.
- Record passing signed campaigns, archive/helper rejection controls, source
  identities and remaining work without treating prefix execution as boot.

The initial combined 34-case campaign stopped at deployment admission after
32 expectations passed. Raft persistence rejected a 9,849-byte log with object
store `ERR_NO_SPACE`. The captured bitmap is consistent with the earlier
sixteen-extent selection limitation despite free space. Preserve that failed
run and validate the full fixture union in separate fresh-guest cohorts; this
does not close the storage or persistence-recovery gate.

CPU discovery/binding, later topology and allocator initialization, sleep,
descriptor I/O, actual stack/capacity, continuous refresh, complete startup,
dependency closure and matching host bootstrap remain open.
