# ERTS 32-bit and word atomics — 2026-11-26

The [atomic adapter](../../../ports/erts/README.md#atomic-operations)
implements the pinned OTP 29.1.2 32-bit and word-sized API families as
C11 inline functions. Each family has thirteen operations in seven barrier
variants plus an address accessor, giving 184 entry points. Their names
match the pinned upstream declarations.

Every variant currently uses sequentially consistent atomic access with
full fences before and after the operation. Exclusive initialization uses
`atomic_init` between those fences. This preserves the full-barrier
contract and supplies stronger ordering for the weaker variants. Native
lock-free integer and word operations are required at compile time.

Unsigned storage and arithmetic preserve wrapping signed bit patterns
without a signed overflowing expression. Exchange, compare/exchange and
bitwise operations return the observed old value. Add-read, increment-read
and decrement-read return the new value. Address accessors borrow the
underlying storage for interoperability; callers retain its lifetime and
follow atomic-object access and alignment rules.

## Qualification

The host value matrix passed with undefined-behavior sanitization and
terminal sanitizer failures enabled. It exercised all seven variants of
every operation in both widths, including successful and failed CAS,
old/new return values, bitwise updates, address layout and arithmetic
crossing the signed minimum and maximum values.

The two-CPU AArch64 QEMU synchronization campaign passed 30 signed cases
and three rejection controls. The new fixture repeated the value matrix,
then updated shared 32-bit and word counters from startup and three
workers. Four thousand updates crossed each signed maximum and produced
the expected final values.

A subsequent worker published plain payload through separate atomic
flags. Startup observed both flags and checked the payload before joining.
The event notification preceded the payload writes, providing no
post-publication event synchronization. The test-only observation loop
has a fixed load bound and one monotonic deadline, with terminal failure;
an atomic flag itself exposes no CQ readiness subscription. This exercise
does not justify polling in production reactors.

All worker arguments and event storage remained live through exact joins,
and final disposal left zero libc allocations. A later signed C application
completed successfully. Existing timed native synchronization, mutex,
condition, read/write, archive-lock, initialization and stack-limit cases
passed in the same campaign. Native Rust sources were unchanged. Python
syntax and diff checks passed.

The [summary](../../plans/evidence/erts-atomics/summary.json) records the
184 supported names, checked source hashes, archive and signed-fixture
identities, host sanitizer settings and pinned upstream sources. The
[serial excerpts](../../plans/evidence/erts-atomics/serial-excerpts.txt)
retain the value, counter and publication results. The signed artifact's
[AArch64 excerpts](../../plans/evidence/erts-atomics/aarch64-barriers.txt)
show `dmb ish` before and after each of the two concrete publication
stores. The finite exercises and selected disassembly support this
implementation; they are not an exhaustive memory-model proof.

## Remaining integration

Double-word atomics and upstream atomic-library initialization remain
open. The standalone aggregate header still needs actual OTP selection.
CPU/runtime initialization, thread-specific events, allocator hooks,
actual stack boundaries, thread progress and coordinated shutdown remain
backend work. Lowering individual variants' fence strength requires a
separate ordering review and qualification. No performance claim is made
for this conservative implementation.

The earlier
[object-store fragmentation finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved. This campaign used a fresh guest and preserves that
failed capture separately from its passing result.

Repeat the signed campaign with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-mutex --smp 2 --output target/erts/atomics-validation
```

The same value matrix can be repeated on a host with the required 64-bit
lock-free C11 atomic types.

```sh
cat > /tmp/erts-atomics-values.c <<'EOF'
#include "atomics-values.h"
int main(void) { check_atomic_values(); return 0; }
EOF
clang -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=undefined \
  -fno-sanitize-recover=all -I ports/erts -I ports/erts/tests \
  /tmp/erts-atomics-values.c -o /tmp/erts-atomics-values
/tmp/erts-atomics-values
```
