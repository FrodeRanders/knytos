# Native ERTS generated metadata and common startup compilation

Observed 2026-11-28 on `feature/erlang-runtime`, based on
`c92565c7125095775b2bc9f99a241ea09e01d6fe` plus the input identities in the
[evidence manifest](../../plans/evidence/erts-generated-metadata/summary.json).

The native metadata profile builds a host YCF generator from pinned OTP
sources, passes its upstream tests and generates all 21 metadata artifacts
through upstream emulator Make rules. Unchanged common `erl_term.c` and
`erl_init.c` compile. The common time supervisor still fails on missing native
CPU-accounting/calendar interfaces. No complete ERTS startup or emulator boot
is claimed.

## Reproduce

```sh
python3 ports/erts/configure.py --native-ethread --native-clock --native-platform \
  --native-metadata --clock-sample-age-ms 5000 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/native-metadata
```

Use a fresh output path without whitespace. OTP remains 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`; Picolibc remains clean 1.8.12 at
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. The
[scope and discovery](../../plans/erlang-m5-generated-metadata.md) record why
real generated metadata is a prerequisite for the common startup/time code.
The earlier native-platform mode remains available without this extra step.

## Host generator and actual target generation

The baseline real `GENERATED` target ran opcode, table, allocator, version and
driver generators, then entered the target internal-library build through
YCF's original prerequisite. That library's generated Makefile was not staged
by the clock qualifier, so the baseline stopped. Its
[capture](../../plans/evidence/erts-generated-metadata/baseline/make.txt)
remains separate from the passing generation.

For native Knytos, the three YCF transform rules now depend on the actual host
YCF executable instead of a target library that the source transformation
does not consume. Unix and Win32 retain the original library prerequisite;
three Make selection controls check that distinction. The generator is built
with host Clang using the pinned sources' standalone Makefile. Before build,
85 build/test inputs are matched against their pinned Git blob identities.
Their SHA-256 identities and the executable hash are also retained.
The upstream YCF test suite passes, including its expected debug/call-transform
failure controls. Its optional Erlang NIF example is not run.

The test environment puts this export's generator wrapper and executable
first on PATH. Emulator generation invokes the pinned executable directly,
avoiding accidental selection of a different installed Erlang runtime's YCF.
This is not a complete host Erlang bootstrap and produces no preloaded BEAM
modules.

Upstream Perl/YCF rules generate opcode headers/source, atom/BIF tables,
allocator metadata, version/driver tables and three yielding-C headers. Make
writes the real `GENERATED` marker only after its eight declared prerequisites
complete. All 21 expected products are present and nonempty; their identities
are recorded. The native opcode rule uses full-width BEAM code pointers via
`-code-model large`, replacing the unexpanded code-model placeholder in the
direct ERTS configure path. This does not enable JIT or set a native compiler
instruction-addressing model. Generated version data reports OTP 29.1.2,
ERTS 17.1.1 and `aarch64-unknown-knytos`.

Clock compilation runs through the generated Makefile without marking
`GENERATED` old. Whole-emulator dependency scanning remains disabled through
the supported `ERTS_SKIP_DEPEND=true`; the optional Git-version include remains
excluded with Make's `-o`. These are unchanged limits on the wider build, not
substitutes for metadata. There are no fabricated generated headers or marker
files.

## Common-source compilation and current gate

With real metadata and the Makefile's common include directories, unchanged
`erl_term.c` and `erl_init.c` compile to AArch64 objects. The native header now
defines the async-pool lower bound as zero, matching upstream's supported
`+A0` path: inspected common `erts_init_async` creates no pool at zero.
This does not qualify positive async-pool sizes, driver execution, thread
capacity or native startup callbacks. Neither common object was linked into
or executed as an ERTS emulator.

The [time-supervisor diagnostic](../../plans/evidence/erts-generated-metadata/common/erl_time_sup.c.txt)
still reports missing `SysTimes`, `sys_times`, `SYS_CLK_TCK`, calendar `struct tm`,
`time` and `sys_localtime_r` integration. The capture is bounded to twelve
diagnostics, so it is not an exhaustive missing-interface inventory. Monotonic
or projected UTC time cannot be substituted for CPU time. No successful dummy
CPU-time reads, POSIX declarations implying implementation or startup stubs
were added to silence these failures.

Four rejection boundaries pass:

- Metadata mode without native-platform mode rejects before source export.
- Changed copied host source rejects before host compilation.
- Missing host YCF rejects when its transform must regenerate.
- Omitting generated headers rejects common term compilation on missing
  `erl_alloc_types.h`.

The missing-tool control uses Make's `-W` on the source without changing its
bytes or timestamp. This is necessary because OTP's empty `.SECONDARY` can
otherwise leave an already-generated header up to date despite an absent
intermediate prerequisite. The control preserves all completed metadata.
Existing clock/thread/interface/platform checks also pass in the final run.

## Runtime scope and artifact identities

The rebuilt three-member clock archive and full-header consumer ELF are
byte-identical to the preceding
[signed two-CPU qualification](2026-11-28-erts-platform-routing.md).
That earlier 20-case campaign and three deployment controls remain the native
runtime evidence by exact archive identity. No new guest execution is claimed.
The new host generator is executed and tested on the development host; common
term/startup compilation is not runtime qualification.

| Artifact | SHA-256 |
| --- | --- |
| Pinned-source host YCF executable | `6ccdcb46b38030ec9e6eff6c03b0326ea53f8a704abbc447581bf6315520a384` |
| Make-built clock archive | `fb17cba4abc6a689868562818818e37816b2de8ff444a759f7d188f2f91bb6fc` |
| Full-header clock consumer ELF | `b6407425a311c073bf6a35c66ef46bff03d7a77f08d516b70c07c7ea3cdb4eb6` |
| Common term object | `3abf798f211bbb816a78f02ab51b3530f7b3ae554293370a81fb376c9152bdd7` |

The manifest records the common startup object's observed identity separately.
Generated marker files may include absolute build paths; no cross-directory
bit-for-bit identity is claimed for every generated metadata file.
Version-controlled evidence retains generation/build logs, generator/source
identities, preflight commands and diagnostics, controls and relevant generated
excerpts. It does not vendor all generated code or binaries. The
[checksums](../../plans/evidence/erts-generated-metadata/checksums.json) cover
the retained evidence. Python syntax, final source identities and
`git diff --check` also pass.

## Development captures and remaining work

Development captures remain separate from the final qualification. The initial
generation run passed its host tests and generated products, but common-source
preflight omitted the emulator target include path and then bundled-library
include directories. Those harness defects first stopped startup compilation
on `erl_version.h` and `zlib.h`. After those paths were supplied, the native
header's missing async-pool minimum was the actual remaining startup compile
requirement; the `+A0` policy above resolves that declaration boundary.

An initial provenance manifest included `.gitignore`, which `git archive`
omits. Verification was restricted to actual build/test inputs, rather than
treating an absent repository-metadata file as source corruption. The first
missing-tool control incorrectly expected an up-to-date transform to rebuild;
the forced-regeneration control corrects that assumption. Failed helper
snapshots, diagnostics and the earlier passing-generation manifest are retained.
Complete original orchestration stdout and every early Python version were
not archived; those captures are not presented as complete source snapshots.
The final qualification's input identities match the final sources.

Native time-supervisor CPU accounting and calendar policy now form a concrete
next integration gate. Native `erts_sys_pre_init` and the other platform entry
points, refresh ownership/terminal policy, I/O/readiness, actual stack bounds,
adequate native thread capacity, floating-point layout qualification, target
internal libraries, full dependency graph and matching host Erlang bootstrap
with preloaded modules remain open. No kernel, capability-owner or cleanup
contract changed. Earlier realtime and object-store incidents retain their
existing dispositions.
