# ERTS upstream header selection — 2026-11-27

OTP's actual `ethread.h` now selects the qualified Knytos adapter surface
through explicit `ETHR_KNYTOS` branches. The staged header compiles in normal,
inline and debug modes. A two-CPU AArch64 signed campaign passed 22 cases
and three deployment rejection controls. This qualifies header selection
for the bounded C adapters; an ERTS executable is not yet built.

## Reproduce

Use OTP 29.1.2 at `d199a49a01a2079afb6f2c5c9ae047cd4555e68d` and clean
Picolibc 1.8.12 at `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`.

```sh
python3 ports/libc/run-tests.py --erts-header --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/header-validation
```

Use a fresh output directory. The runner first executes the
[interface checker](../../../ports/erts/README.md#upstream-header-selection),
which exports pinned Git objects, applies the checked-in header patch
without fuzz and copies the adapter headers into `internal/knytos/`.
The source checkout is untouched. Integer sizes come from target compiler
builtins; cache-line size uses OTP's existing 64-byte assumption. This
explicit compile-probe profile supplies no Autoconf feature answers.

## Selected contract

OTP retains its common scalar typedefs, thread options, allocator/init
layouts, thread/TSD declarations and thread-event structure and flags.
The Knytos branches select native non-owning identities, C11 atomics,
events, mutexes and read/write mutexes. Native identity definitions are
shared with the standalone adapters; ownership remains in their existing
generation-qualified records and native owners.

Low-level lock and thread-event calls remain external even when a consumer
requests `ETHR_TRY_INLINE_FUNCS`. This preserves ownership, busy/peek and
allocator reentry checks. OTP's pthread inline implementations cannot
replace them. Conflicting pthread/Win32 selection and absence of the
worker-libc profile produce explicit compile errors. Private bootstrap
helpers are exposed separately by `knytos/ethr_extensions.h`.

The checker matched all 253 exposed functions, six scalar typedefs, nine
portable records, two option enums and seven public constants. All retained
function references resolved through the seven-member archive. Compiler
dependencies identify the staged OTP header and Knytos branches, with no
pthread/Win32 header path. Deliberate signature, field-type, conflicting
backend and missing-profile controls all rejected.

## Signed qualification

QEMU 11.1.3 ran the minimal release guest with two CPUs. The signed fixtures
compiled through the selected OTP header with inlining requested:

- Low-level lock contention, shared readers, writer wakes, TSD and exact joins.
- All 184 word/32-bit atomic operations, wrapping, contended arithmetic and
  cross-CPU publication before join.
- Independent temporary event owners, busy/peek retention, partial clear,
  bounded admission, destructor rearming, explicit exit and detached drain.
- Captured allocator disposal, failed preparation prefixes, rejected callback
  reentry and per-participant main status through normal and explicit exit.
- The same thread-event lifecycle with `DEBUG` enabled.

The runner generates copies of the existing fixtures, replacing only the
aggregate include with the probe profile, OTP header and private extensions.
Their test logic is unchanged. Each selected fixture ended with zero live
allocation blocks; the final survivor application completed. Baseline native
worker and synchronization fixtures and the TLS, unavailable `open` and
tampered-signature rejection controls also passed.

The selected OTP header has SHA-256
`a3868a669603dc737bd51a433ebca66a76906f01dc36f3f1cb50b26c80c3099f`.
The adapter archive has SHA-256
`236b19b2f8ce831a383bb27f0aea29605d3a82fb1b2986891325bfffeb780679`.
The [summary](../../plans/evidence/erts-header-selection/summary.json) records
source, compiler, kernel, firmware, member and signed fixture identities.
The [generated consumer](../../plans/evidence/erts-header-selection/contract.c),
[dependencies](../../plans/evidence/erts-header-selection/header-dependencies.txt),
[compiler controls](../../plans/evidence/erts-header-selection/interface-diagnostics.txt),
[serial excerpts](../../plans/evidence/erts-header-selection/serial-excerpts.txt)
and [link excerpts](../../plans/evidence/erts-header-selection/link-map-excerpts.txt)
retain the relevant diagnostics. Python syntax and diff checks passed.

## Failed build and remaining work

The first signed campaign attempt stopped before boot:

```text
fatal error: 'atomics-values.h' file not found
```

The generated fixture had lost its original source-directory include path.
The runner now supplies that directory. The
[failed capture](../../plans/evidence/erts-header-selection/failed-fixture-build/summary.json)
and [diagnostic](../../plans/evidence/erts-header-selection/failed-fixture-build/diagnostic.txt)
remain separate from the passing repeat. No guest cases ran in that attempt.

Full `ethr_init`/`ethr_late_init`, fatal/assert support, runtime configuration,
double-word atomics and ERTS's standalone memory-barrier macros remain open.
This stage does not select the backend through configure or the upstream
Makefiles. Configure still rejects its missing thread library. Native thread
capacity, actual stack boundaries and thread-progress/shutdown integration
also remain open.

This separate fresh campaign does not close the earlier
[combined-campaign storage fragmentation failure](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign).
