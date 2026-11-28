# M5 generated emulator metadata

This startup prerequisite removes the clock build's excluded BEAM-generator
dependency. It generates pinned ERTS metadata using upstream Make rules and
a host YCF executable built from the same OTP revision. It does not supply a
complete host Erlang bootstrap or implement native startup with dummy hooks.

**Status:** qualified within this scope. The pinned host generator's upstream
tests pass, all 21 metadata artifacts are generated, and unchanged common
`erl_term.c`/`erl_init.c` compile. The time-supervisor preflight still rejects
missing native CPU-accounting/calendar interfaces. See the
[qualification](../reports/milestones/2026-11-28-erts-generated-metadata.md).

## Discovery

The native `erts_sys_pre_init` path must eventually initialize upstream
threading and the common time supervisor. The latter includes `global.h` and
depends on genuine generated opcode, atom, BIF and allocation tables. Its
CPU-accounting/calendar requirements are also still platform work.

The current native clock qualifier deliberately marks `GENERATED` old. OTP's
real target also produces three yielding-C headers using a host YCF generator.
The original cross rule discovers YCF through an installed Erlang runtime and
depends on the target internal-library build, although YCF transforms source
and does not consume that library. Reusing a different installed OTP's YCF
would not establish the pinned generator's provenance.

## Acceptance

- Add explicit native metadata mode after native platform selection.
- Build the pinned exported YCF sources with the host compiler and upstream
  standalone Makefile. Record host source/tool/executable identities.
- Use upstream target Make rules to produce the complete `GENERATED` set,
  including YCF transforms, with native source selection and actual generator
  prerequisites. Do not fabricate markers, generated headers, host `.beam`
  modules or successful POSIX feature answers.
- Rebuild/link the native clocks without excluding `GENERATED`; preserve the
  qualified native archive by exact identity where possible.
- Compile selected common ERTS sources far enough to record the next actual
  native platform gates, keeping diagnostics separate from passing generation.
- Permit upstream's zero-sized async-pool lower bound in the initial headless
  profile, and compile unchanged common term/startup sources. This does not
  qualify positive async-pool sizes or execute native ERTS startup.
- Check negative mode/source-identity controls and update the current plan and
  qualification record. No emulator boot or runtime-tested common source is
  inferred from generation or compilation.

Full host bootstrap/preloaded modules, native startup, refresh ownership,
CPU accounting/calendar behavior and the complete emulator build remain open.
