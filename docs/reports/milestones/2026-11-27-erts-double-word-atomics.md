# ERTS native double-word atomics — 2026-11-27

The Knytos adapter now supplies all thirty pinned double-word entry points:
init, set, read and compare/exchange in seven barrier forms, address lookup
and the native-capability query. Both the standalone and selected OTP
headers expose them. The signed AArch64 header campaign passed 26 cases
and three deployment rejection controls.

## Contract and representation

The implementation uses the target compiler's native 128-bit `__atomic`
builtins. A compile-time check rejects targets without always-lock-free
16-byte operations. A separate full fence with compiler clobbers surrounds
each operation; the weaker variants receive the same stronger ordering.
Exclusive initialization copies the value between fences without reading
uninitialized backing. There is no per-object allocation, blocking lock or
`libatomic` fallback. Native retries are lock-free, not wait-free.

The pinned OTP GCC native backend explicitly allows word-aligned objects
and reserves an extra word for internal alignment. The Knytos object uses
three signed words: 24 bytes with 8-byte base alignment. Its accessor selects
a 16-byte-aligned pair inside that object. A native `may_alias` integer
type permits the composite access to adjacent word objects under the
[documented compiler extension](https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html).
The original object must remain at its address throughout its users;
bitwise relocation after initialization is not supported. The returned
word pointer is borrowed and ordinary access requires external exclusion.

The value union retains `sint[2]`, with little-endian low/high indices.
Bit copies preserve signed patterns. Compare/exchange returns a boolean,
keeps expected unchanged on success and replaces both expected words with
one observed pair on failure. New and expected value pointers may alias
one another, while remaining separate from the atomic backing.

The qualified compiler emits `ldaxp`/`stlxp` retry loops. Reads validate
their pair using an exclusive writeback, including after a mismatched
compare/exchange. Even reads therefore require writable atomic backing.
This native implementation adds no CQ waiting or blocking mutex adapter.

## Qualification

The host matrix passed under Clang's undefined-behavior sanitizer with
terminal failures enabled. For every barrier variant it checked:

- Objects at both 16-byte and 8-byte base alignment, internal address
  alignment, preserved padding and surrounding guards.
- Signed minimum/maximum patterns, independent mismatches in each half,
  coherent expected replacement and success preserving expected.
- Aliased new/expected value arguments and exclusive raw-word access.

The two-CPU QEMU 11.1.3 campaign passed both inline and debug fixtures.
Startup and three retained workers completed 2,048 successful contended
increments of a complementary-word pair. Failed CAS observations also
preserved the pair invariant. The same workers then performed 6,144 stores
while startup read and checked paired values. A publisher wrote payload
after its completion-counter update, then published a distinct pair.
Startup observed that pair and checked payload before joining; completion
counting and phase events did not publish those later payload writes.

All nine events, callback arguments and worker records remained retained
through exact joins. Phase hints established the earlier atomic-loop
boundary, not callback completion or event-setter quiescence. The bounded
test-only observation loops shared one monotonic deadline and terminal
failure policy; they do not establish a production polling contract or
bound the compiler's internal exclusive retries. Both fixtures ended with
zero live libc blocks. The final survivor completed.

The linked-ELF checker verified native set, read and CAS loops, surrounding
`dmb ish` fences, read writeback validation and the CAS mismatch validation
path. Only finite `memcpy` helpers accompany these native operations.
No out-of-line atomic helper is accepted. Existing barrier instruction
checks and the earlier atomic, synchronization, event and allocator
fixtures passed in the same campaign.

The interface checker matched 283 functions, six scalar typedefs, nine
portable records, the double-word value union, two enums and thirteen
constants in normal, inline and debug modes. All four interface rejection
controls passed. The standalone check also passed. Native Rust sources
were unchanged; Python syntax and diff checks passed.

The [summary](../../plans/evidence/erts-double-word-atomics/summary.json)
records checked source, compiler, kernel, firmware, archive, signed fixture
and sanitizer identities. The
[native instructions](../../plans/evidence/erts-double-word-atomics/aarch64-double-word.txt),
[serial excerpts](../../plans/evidence/erts-double-word-atomics/serial-excerpts.txt),
[compiler controls](../../plans/evidence/erts-double-word-atomics/interface-diagnostics.txt)
and [generated consumer](../../plans/evidence/erts-double-word-atomics/contract.c)
retain the relevant evidence. Finite tests and selected instruction checks
are not an exhaustive memory-model proof.

## Reproduce and remaining integration

Use OTP 29.1.2 at `d199a49a01a2079afb6f2c5c9ae047cd4555e68d` and clean
Picolibc 1.8.12 at `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`.

```sh
python3 ports/libc/run-tests.py --erts-header --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/double-word-validation
```

Use a fresh output directory. The host value matrix requires a compiler
target with native lock-free 128-bit operations enabled:

```sh
clang -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=undefined \
  -fno-sanitize-recover=all -I ports/erts -I ports/erts/tests \
  docs/plans/evidence/erts-double-word-atomics/host-value-matrix.c \
  -o /tmp/erts-dw-values
/tmp/erts-dw-values
```

Full upstream initialization and native-operation introspection remain
open, along with fatal/assert/runtime support, actual stack boundaries,
thread-progress/shutdown integration and upstream build selection.
Configure still rejects its missing thread library. No ERTS executable
is built by this campaign.

The earlier [sequential-admission and interleaved-log captures](2026-11-27-erts-memory-barriers.md#failed-sequential-admission-fixture)
remain historical failures with their stated limitations. This passing
campaign also does not close the
[combined-campaign storage fragmentation finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign).
