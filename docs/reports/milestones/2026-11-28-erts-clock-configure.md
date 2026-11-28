# Native ERTS clock configure selection

Observed 2026-11-28 on `feature/erlang-runtime`, based on
`3326f9f0bb105af3fc7e6e3d8adeb361a49f6de8` plus the source hashes in the
[evidence manifest](../../plans/evidence/erts-clock-configure/summary.json).

The explicit native thread/clock configure probe completes with exit zero and
generates OTP headers and Makefiles. M5 remains incomplete: common `sys.h`
and the emulator Makefile still select Unix platform code. No emulator was
built or executed in this change.

## Reproduce

```sh
python3 ports/erts/configure.py --native-ethread --native-clock \
  --clock-sample-age-ms 5000 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/clock-configure
```

Use a fresh output directory without whitespace. Inputs remain OTP 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d` and Picolibc 1.8.12 at
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. Host Autoconf regenerates the
exported pinned configure script. The source checkout is not patched.
The [implementation scope](../../plans/erlang-m5-clock-configure.md) records
the discovery and acceptance boundary.

The opt-in mode requires native ethread and an explicit sample age. It builds
the service-worker C SDK, the three-member native clock archive and the actual
pinned time-record probe. A separate Autoconf branch checks the native target,
thread backend and age policy, then genuinely compile-links twelve retained
clock/startup/performance-counter references. The pointers remain referenced
under optimization and section garbage collection. The resulting static
AArch64 ELF passes the C SDK's loader-layout checks.

Configure emits `ERTS_KNYTOS_CLOCK`, the selected finite sample-age policy and
`HAVE_MONOTONIC_ERTS_SYS_HRTIME`. It emits no POSIX system/monotonic/high-resolution
backend, corrected-clock, procfs CPU-time or `clock_gettime` CPU-time selection.
The native branch does not run the Unix time-correction feature search or
provide cached successful POSIX answers. The generated header and native
ethread Make source selection are checked directly. The macro also emits an
explicit warning that native sys routing and refresh ownership remain open.

## Results and controls

| Invocation | Result |
| --- | --- |
| Final default | Exit 1 at the original thread-library gate |
| Final native ethread only | Exit 1: `No wall clock source found` |
| First native clock, original flags | Clock link passes; exit 1 because CFLAGS lacks an optimization flag |
| Final native ethread and clock, explicit `-Os` | Exit 0; configuration outputs generated and checked |

The first clock run's [configure capture](../../plans/evidence/erts-clock-configure/initial-no-optimization/configure.txt)
is preserved separately, together with its toolchain, staged macro, link
consumer/header and artifact hashes. OTP requires an explicit `-O` flag;
`-Os` matches the existing adapter build profile. A passing optimized run
does not erase the earlier stop. The initial Python orchestration/helper
versions were not snapshotted; their complete source identities cannot be
reconstructed from that capture. The final run's complete recorded source
hashes were checked against the working tree.

Four link/compile controls reject absent archive, mismatched age policy,
missing service-worker profile and an incorrect expected system-time signature.
The signature assertion rejects even without `-Werror`.
An independent harness executes the production native m4 macro with the same
target compiler/SDK: successful selection emits the expected definitions and
empty `LIBRT`; nine controls reject the wrong target, wrong thread backend,
missing/zero/nonnumeric/oversized/overflowing/leading-zero age and a POSIX
clock choice. Five CLI controls reject invalid mode/age combinations before
source export or SDK construction. Existing ethread retained-reference,
source/archive identity and rejection checks also pass.

The unmodified startup checker was run as a baseline:

```sh
python3 ports/erts/check-time-init.py --otp-source /path/to/otp29 \
  --picolibc-source /path/to/picolibc --output /fresh/time-init-baseline
```

It verifies fourteen record fields, three rejection controls, nine host
scenarios under undefined-behavior sanitization and a native static link.
No Rust or native runtime implementation changed. Both rebuilt native
archives are byte-identical to their earlier signed qualifications:
[ethread](2026-11-27-erts-native-bootstrap.md) and
[startup clocks](2026-11-27-erts-time-init.md). No new guest execution is
claimed. Python syntax checks and `git diff --check` also pass.

| Artifact | SHA-256 |
| --- | --- |
| Native ethread archive | `0ef39934fc1a745557a38ad46300e8b969bd9f50a01a2361b70b116d5bdc9fba` |
| Three-member clock archive | `7ce10a790ddaf566196cac8838de880dbf916efc0ff64fa417a15bd21790b15c` |
| Actual optimized Autoconf clock link ELF | `fadd7e0d1c5d809630bf5cfeb4c7c4cd03114c0f9e946709df02b45f3f878275` |
| Regenerated configure | `58d8d8de462ad01c5250772af0c82120c78eccfe973cbafb319aa2dd57e62fee` |

The version-controlled evidence retains configure logs, actual link sources,
selected headers, negative diagnostics, generated configuration and manifests.
Its [checksums](../../plans/evidence/erts-clock-configure/checksums.json) cover
the retained files. Binary hashes identify the observed artifacts; the link
ELF itself is not versioned.

## Remaining integration and limits

The selected SDK still uses `KNYTOS_ERTS_TIME_CONTRACT_PROBE`, extracting the
real record rather than compiling the complete `sys.h`. Common `sys.h` still
includes `erl_unix_sys.h`; generated emulator Make still reports
`ERLANG_OSTYPE = unix`, `OPSYS=noopsys` and selects Unix `sys_time.o`.
The native clock sources are not routed into that emulator graph. The
configure probe's relative SDK paths also need propagation through the actual
build layout and top-level wrapper. Generated Makefiles alone do not qualify
that graph, its hardening/compiler flags or a full ERTS build.

Full native startup must call the initializer before statusless reads, with
the signed time grant and qualified enclosing context. Its explicit five-second
sample-age window is a probe policy, not a refresh schedule or UTC accuracy
bound. A complete notification-backed refresh owner, startup/shutdown handling,
metadata update policy and expiry behavior remain mandatory before continuous
emulator use. No owner, kernel resource, admission or teardown contract changed.

Configure also warns `unknown double endianness`; no floating-point layout
qualification is inferred from its successful exit. Matching host bootstrap,
native platform startup/I/O, allocator and CPU-accounting integration, actual
stack bounds, thread capacity/progress and signed BEAM execution remain open.
The earlier object-store extent incident and failed realtime captures retain
their existing unresolved dispositions; this configure result does not
explain them.
