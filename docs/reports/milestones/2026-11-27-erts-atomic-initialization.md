# ERTS atomic initialization and introspection — 2026-11-27

The native-only backend now implements `ethr_init_atomics` and the four
native-operation inventory entry points. The AArch64 signed header campaign
passed 28 cases and three deployment rejection controls. Full ERTS startup
initialization remains separate work.

## Contract

Upstream's atomic initializer prepares address-indexed fallback locks when
scalar native operations are unavailable. This backend requires native
lock-free operations at every supported width and contains no fallback
lock array. Its initializer therefore has no mutable setup work: it
returns zero without allocating or resetting existing values. Calls may
repeat concurrently, including while atomic objects remain active.

Scalar native types alias the qualified 32-bit and word representations.
Their full-barrier callable names map to the existing implementations,
including `add_return`, `inc_return`, `dec_return` and `and/or_retold` names
used by the native interface. Public double-word forms now share the native
word-pair init, set, read, compare/exchange and address implementations.
The native functions retain the prior ordering, value and alignment
contracts; no blocking primitive or weaker fence is introduced.

The 32-bit and 64-bit inventories each report thirteen `_mb` operation
names. The pair inventory reports four. The optional scalar single-unit
double-word native interface remains absent and returns an empty inventory.
Only callable source-level names are reported. Scalar aliases need not
produce separate ELF symbols. Implementation-name macros allow upstream's
`ethread_info` BIF to name the providers. Its table-consumption contract
was reviewed in the pinned `erl_bif_info.c`; an Erlang BIF was not executed
by this C campaign. Native spinlocks remain unadvertised.

The legacy `char **` API returns borrowed, immutable tables and strings
with image lifetime. Callers must not modify or free them. The provider
does not allocate per-participant snapshots or inspect mutable thread
state. The new `ethr_atomic_info.c` is the archive's eighth member.

## Qualification

QEMU 11.1.3 ran the minimal release guest with two CPUs. Inline and debug
fixtures checked every reported primitive name by calling its native
interface. Both scalar families exercised old/new return conventions,
mutation and borrowed addresses. Pair primitives checked both base
alignments, successful and failed compare/exchange and expected replacement.
Inventories were bounded, null-terminated, unique and stable before and
after initialization; the optional single-unit table was empty.

Startup and three workers repeatedly initialized and inspected the backend
while operating on their own native objects. Initialization preserved an
existing shared counter, which finished with the expected 96 worker
increments. Borrowed table identities remained stable. Arguments and gates
stayed live through exact joins, and both fixtures ended with zero live
libc allocations. The final survivor completed.

Both existing host value matrices passed Clang undefined-behavior
sanitization after the native pair refactor. The signed campaign also
repeated the word/32-bit and pair value matrices, contended CAS, publication,
memory-barrier, synchronization, event and allocator cases. All four
linked-ELF instruction checks passed. The native pair paths retained
exclusive-pair validation loops and full surrounding fences.

The interface checker matched 288 public functions against pinned OTP
prototypes and linked 33 native callable names separately, for 321 retained
references through all eight archive objects. It checked the existing
scalar, record, enum, value-union and constant layouts in normal, inline
and debug modes. All four interface rejection controls passed, as did the
standalone checker. Native Rust sources were unchanged. Python syntax and
diff checks passed.

The [summary](../../plans/evidence/erts-atomic-initialization/summary.json)
records source, compiler, kernel, firmware, archive, signed-fixture and host
sanitizer identities, plus the exact native inventories. The
[serial excerpts](../../plans/evidence/erts-atomic-initialization/serial-excerpts.txt),
[compiler controls](../../plans/evidence/erts-atomic-initialization/interface-diagnostics.txt),
[generated consumer](../../plans/evidence/erts-atomic-initialization/contract.c),
[pair instructions](../../plans/evidence/erts-atomic-initialization/aarch64-double-word.txt)
and [link excerpts](../../plans/evidence/erts-atomic-initialization/link-map-excerpts.txt)
retain the relevant evidence. The archive SHA-256 is
`3927fcfa0752f57fc7f0dbd0e63edfbf32cbbb7dd0793b2196314f22a8d19e06`.

## Reproduce and remaining work

Use OTP 29.1.2 at `d199a49a01a2079afb6f2c5c9ae047cd4555e68d` and clean
Picolibc 1.8.12 at `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`.

```sh
python3 ports/libc/run-tests.py --erts-header --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/atomic-initialization-validation
```

Use a fresh output directory. Repeat the host matrices on a compiler target
with native lock-free 128-bit operations enabled:

```sh
clang -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=undefined \
  -fno-sanitize-recover=all -I ports/erts -I ports/erts/tests \
  docs/plans/evidence/erts-atomic-initialization/host-value-matrix.c \
  ports/erts/ethr_atomic_info.c -o /tmp/erts-atomic-values
/tmp/erts-atomic-values
```

This closes native-only atomic setup and operation introspection, not
`ethr_init`/`ethr_late_init`. CPU/topology and full runtime initialization,
fatal/assert support, actual stack boundaries, broader native-thread
capacity, thread-progress/shutdown and upstream build selection remain
open. Configure still rejects its missing thread library. No ERTS
executable is built by this campaign.

Earlier [sequential-admission and interleaved-log captures](2026-11-27-erts-memory-barriers.md#failed-sequential-admission-fixture)
retain their stated limitations. This fresh passing campaign also does
not close the [combined storage-fragmentation finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign).
