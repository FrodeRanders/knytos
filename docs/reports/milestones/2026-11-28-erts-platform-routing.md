# Native ERTS platform header and clock source routing

Observed 2026-11-28 on `feature/erlang-runtime`, based on
`3326f9f0bb105af3fc7e6e3d8adeb361a49f6de8`, the preceding native-clock
configure work and the source identities in the
[evidence manifest](../../plans/evidence/erts-platform-routing/summary.json).

The explicit native-platform profile compiles complete pinned common
`sys.h` and selected native `ethread.h`, builds the three clock objects through
the generated emulator Makefile and links a checked consumer. All 20 signed
cases and three deployment controls pass on two guest CPUs. This advances M5;
no complete ERTS emulator has been built or booted.

## Reproduce and scope

```sh
python3 ports/erts/configure.py --native-ethread --native-clock --native-platform \
  --clock-sample-age-ms 5000 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/native-platform
python3 ports/libc/run-tests.py --erts-time-init \
  --erts-platform-build /fresh/native-platform --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/native-platform-validation
```

Use fresh output paths without whitespace. OTP remains 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`; clean Picolibc remains 1.8.12 at
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. Host Autoconf 2.73 and GNU Make
3.81 were used. Source acquisition and patches operate on an exported OTP
tree. The upstream checkouts are not modified.

The [implementation scope](../../plans/erlang-m5-platform-routing.md) defines
the boundary. The original complete-header compile failed because upstream
`sys.h` selected `erl_unix_sys.h`, which required unavailable `netdb.h`.
The [baseline capture](../../plans/evidence/erts-platform-routing/baseline/qualified-baseline.txt)
retains that failure. An earlier baseline command also redundantly defined the
age policy already in generated config; that harness error and corrected
baseline remain separate.

## Header, source and build selection

The new opt-in requires native ethread and clock SDKs. It selects `ERTS_KNYTOS`,
`ERLANG_OSTYPE=knytos` and `OPSYS=knytos`. Common upstream `sys.h` includes
`erl_knytos_sys.h`, which supplies standard C prerequisites, the existing
clock surface and a scalar remaining-duration record. It supplies no Unix
descriptor, signal, process or CPU-accounting backend. Unimplemented native
operations retain upstream declarations; no success stubs were added.

The generated emulator Makefile selects exactly `erts_monotonic.o`,
`erts_system_time.o` and `erts_time_init.o` for its native platform list, with
no Unix drivers or fallback polling objects. The qualifier evaluates the
actual Makefile and builds those objects using its actual source rule and
configured flags. Four fragment comparisons preserve the original Unix and
Win32 lists with fallback polling both enabled and disabled; both native
choices select only the clocks.

Generated configuration is copied to OTP's expected in-tree build locations.
The SDK paths are absolute for this mode, so the compiler also works from
the emulator directory. `_GNU_SOURCE` exposes implemented libc declarations,
including `memrchr`, whose symbol configure actually links. No successful
POSIX feature answers are supplied.

Only the clock portion of the Make graph is qualified. OTP's supported
`ERTS_SKIP_DEPEND=true` excludes the whole-emulator dependency scan; Make's
`-o` excludes the unneeded BEAM generator prerequisite and Git-version
include. No empty generated headers or success markers are fabricated.
The logging-only `output.mk.in` is rendered with explicit verbosity because
the top-level wrapper has not run. These exclusions do not qualify a full
host bootstrap, generated BEAM metadata or complete emulator dependency graph.

The native clock objects retain configured stack-protector, branch-protection
and other compiler flags. Their archive differs from the earlier standalone
archive; prior signed execution is not reused as proof for these objects.
Picolibc's linked stack-guard constructor runs through the existing C startup
constructor path. The signed fixtures check its guard is nonzero before clock
calls. This does not prove random/secret canaries or general hardening coverage.
The Make build reports C90 declaration-order warnings from C11 adapter code;
there are no compile errors in the final clock build. Full-platform compiler
flag policy remains part of later integration.

## Interface and runtime qualification

The [consumer](../../plans/evidence/erts-platform-routing/platform/consumer.c)
checks record size/alignment, all fourteen scalar field types/offsets and
twelve clock/startup/performance-counter interfaces. Its expected record comes
from pinned upstream text, while its actual record comes from complete common
`sys.h`. The retained references link against the Make-built archive in a
validated static AArch64 ELF.
The [dependency capture](../../plans/evidence/erts-platform-routing/platform/consumer.d)
confirms complete `sys.h`, `erl_threads.h`, native `ethread.h` and the Knytos
platform header, excluding `erl_unix_sys.h`, `erl_win_sys.h` and
`erts_time_contract.h`. No substitute common or thread header is used.

Five compile controls reject Unix selection, missing service-worker profile,
the extracted-record probe, descriptor-readiness requests and a changed
expected record-field type. Three actual configure controls reject a missing
clock SDK, wrong target and invalid mode. A fourth configure run explicitly
disables the native platform and completes with the original Unix platform
selection and native clock link probe. The existing thirteen clock and two
thread rejection controls also pass in the final native run.

The signed campaign builds fixtures through the complete headers and links
the Make-built archive. Only the unused-parameter diagnostics in upstream
headers are scoped out of the fixture builder's extra warning policy; fixture
errors remain fatal. The archived fixture sources and build manifests retain
that scope. There is no extracted-record probe define in these fixtures.

All 20 cases pass: the general C runtime, constructor/exit paths, native workers,
mutex/condition, read/write lock, event, clock startup, missing-grant denial,
terminal startup failure and later survivor. TLS, unsupported `open` linkage
and tampered signature controls reject. The signed time grant is present only
for the successful clock fixture. The relevant serial excerpts are retained
in [version-controlled evidence](../../plans/evidence/erts-platform-routing/serial-excerpts.txt):

```text
ERTS TIME INIT pinned metadata local performance counter and worker reads PASS
ERTS TIME INIT missing grant preserves metadata and cold clock PASS
[erts] clock initialization failed
[c-runtime] abort
```

The success fixture checks immutable startup metadata, local performance
counter units and clock reads on startup and one worker through exact join.
Denial preserves the entire metadata record and cold clock state. Terminal
startup failure aborts its domain; a later survivor succeeds. No new kernel
or runtime resource owner, teardown policy or capability ABI was introduced.
Zero tracked C blocks and exact joins do not establish physical root retirement.

| Artifact | SHA-256 |
| --- | --- |
| Make-built three-member clock archive | `fb17cba4abc6a689868562818818e37816b2de8ff444a759f7d188f2f91bb6fc` |
| Complete-header link ELF | `b6407425a311c073bf6a35c66ef46bff03d7a77f08d516b70c07c7ea3cdb4eb6` |
| Signed success fixture | `2cdf101a075f6183fc7c86823ec7d6a948a9738cf019bfe93b0d4e4e6d382cb3` |
| Kernel | `7e429c4a38ba280937501fd02a2b303028065a7deb43a148bfa3447168b483fb` |

The manifest includes compiler versions, member/input hashes, fixture and
descriptor identities, guest CPU count, QEMU 11.1.3, kernel/firmware/image
identities and source snapshots. The
[checksums](../../plans/evidence/erts-platform-routing/checksums.json) cover
the retained evidence. Modified inputs from the preceding configure qualification
were recovered and hash-matched before being archived separately as baseline
inputs; their earlier recorded results were not rewritten.

## Failed attempts and remaining gates

The first Make attempt used an incorrect relative config-header dependency and
left the whole-emulator dependency scan enabled while excluding BEAM generation.
Its [original capture](../../plans/evidence/erts-platform-routing/initial-make/make.txt)
contains the unavailable descriptor-readiness diagnostic, allocation-metadata
preprocessor checks and the missing config-header target. Those scan messages
are not a valid complete-build qualification or evidence of an allocator defect:
the required generation phase was deliberately excluded by this clock harness.
Correcting the dependency path and scope allowed the clock rules to build.
The earlier capture remains separate. The next inspection attempt used
`make --eval`, unsupported by host Make 3.81; the portable additional Makefile
replaced it. Its attempted command and relevant diagnostic are retained, but
the complete original orchestration stdout was not archived. Early Python
helper versions were not snapshotted; final qualified source identities are
complete.

The first guest attempt could not boot because sandbox policy denied QEMU's
local QMP socket. Its QEMU/LLDB diagnostics and failed result are retained
[separately](../../plans/evidence/erts-platform-routing/sandbox-blocked/qemu.txt).
The passing repeat ran with the required execution permission. It does not
reclassify that original attempt as guest execution.

Native ERTS startup must still supply the rest of the platform and invoke time
initialization in its real lock/lifecycle context. This campaign invokes the
time entry point from a C fixture, not an ERTS scheduler. Continuous refresh
needs a complete notification-backed owner and terminal shutdown/expiry policy;
the explicit five-second sample window installs no producer. Startup metadata
update policy, CPU accounting, I/O/readiness, remaining generated metadata,
full allocator/build integration, actual stack bounds, floating-point layout
qualification and matching host bootstrap remain open. Configure still warns
`unknown double endianness`; no floating-point qualification is inferred.

Earlier object-store extent and failed realtime captures retain their existing
unresolved dispositions. This qualification neither explains nor closes them.
