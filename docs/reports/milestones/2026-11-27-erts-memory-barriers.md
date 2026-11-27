# ERTS standalone memory barriers — 2026-11-27

The Knytos adapter now supplies OTP's standalone data-barrier interface
through both its aggregate header and the staged upstream `ethread.h`.
The four ordering-direction constants match the pinned OTP GCC backend.

## Ordering contract

`ethr_membar.h` supplies `ETHR_MEMBAR`, full, write, read and dependency-read
barriers, plus the separate compiler barrier. Every data variant uses the
legacy `__sync_synchronize()` full-barrier contract permitted by OTP's
`gcc/ethr_membar.h`. It retains that backend's compiler clobbers before and
after the builtin. This implementation deliberately exceeds the weaker
variants' minimum requirements, including issuing a full fence for mask
zero. The mask argument is evaluated once and the macro is a void expression.

The compiler barrier is an empty volatile assembly statement with a memory
clobber. Existing word and 32-bit C11 atomic operations retain their prior
fences and memory orders. No dependency-ordering shortcut or instruction
barrier is advertised. Data ordering establishes neither instruction-cache
synchronization nor device completion or resource retirement.

## Reproduce

Use OTP 29.1.2 at `d199a49a01a2079afb6f2c5c9ae047cd4555e68d` and clean
Picolibc 1.8.12 at `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`.

```sh
python3 ports/libc/run-tests.py --erts-header --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/barrier-validation
```

Use a fresh output directory. The existing header-selection profile now
includes inline and debug barrier fixtures. Before signing, the runner
checks their linked ELF instructions with `ports/erts/check-barriers.py`.
Each of the four named data barriers and sixteen constant-mask sites must
contain `dmb ish` followed by return. The compiler-only site must preserve
store/reload/store without adding a CPU barrier.

## Qualification

The final two-CPU AArch64 campaign on QEMU 11.1.3 passed 24 signed cases
and three deployment rejection controls. Its interface checker compiled
normal, inline and debug modes, checked the 253 exposed functions, nine
portable records and eleven constants, and rejected all four signature,
layout, backend and profile controls. The standalone checker also passed.

Both barrier fixtures passed the linked-ELF checks for all twenty data-fence
sites and the separate compiler barrier. They exercised all sixteen masks,
single argument evaluation and statement/expression use. A single publisher
and startup exchanged 1,024 payloads across four publication modes: full,
write/read, full/dependency-read and explicit-mask barriers. Payload and
flags use relaxed C11 atomics, avoiding a data race even if ordering fails.
Startup checked each payload before acknowledgement and before producer
return or join. The only event notification preceded publication.

Both participants retained one monotonic ten-second deadline and a finite
load bound throughout the accepted publisher's four modes. This bounded
fixture observes flags with no CQ readiness subscription; it establishes no
production polling policy. Final exact join fenced argument and event
disposal. Both fixtures ended with zero live libc blocks. The existing
atomic, lock, thread-event, allocator and native-worker cases passed too,
and the final survivor application completed. The corrected unjoined-worker
control observed its runtime and kernel-abort diagnostics without a normal
C exit. Python syntax and diff checks passed; native Rust sources were
unchanged.

The [summary](../../plans/evidence/erts-memory-barriers/summary.json) records
checked source, toolchain, kernel, firmware, archive and signed fixture
identities, including both instruction-check results. The
[AArch64 sites](../../plans/evidence/erts-memory-barriers/aarch64-barriers.txt),
[serial excerpts](../../plans/evidence/erts-memory-barriers/serial-excerpts.txt),
[compiler controls](../../plans/evidence/erts-memory-barriers/interface-diagnostics.txt),
[generated consumer](../../plans/evidence/erts-memory-barriers/contract.c)
and [link excerpts](../../plans/evidence/erts-memory-barriers/link-map-excerpts.txt)
retain the relevant evidence. The selected header SHA-256 is
`397b09c1c34825ebb5b29881ad9bfc0eac5e1aae0b758d8e240c6dec29007470`.

## Failed sequential-admission fixture

The first guest campaign passed 21 cases and three deployment controls,
then the new barrier fixture aborted on this assertion:

```text
assertion "ethr_thr_create(&tid, publisher, &input, NULL) == 0" failed
```

That fixture's descriptor allowed two native threads and the fixture tried
four sequential publisher generations, joining each one. The
[failed capture](../../plans/evidence/erts-memory-barriers/failed-sequential-admission/summary.json),
[serial](../../plans/evidence/erts-memory-barriers/failed-sequential-admission/serial-excerpts.txt)
and original fixture/header snapshots remain separate from the repeat.
The assertion did not retain the returned error code or barrier mode;
a specific runtime cause is not established by this capture.

The final fixture retains one admitted publisher and its complete argument
and event storage across all four modes, then confirms its exact join.
This removes admissions that served only to separate test phases. It does
not establish immediate kernel thread-capacity reuse after join: callback
exit observation and kernel retirement remain separate contracts.

## Interleaved unjoined-control logs

The next campaign stopped after 11 cases, before reaching the barrier
fixtures. Its unjoined-worker control produced interleaved console text:

```text
lp=1 asid=29 WWORKER private queue entry
lp=0 asid=29 ORKER expected unjoined exit
lp=0 asid=29 [c-runtime] exit with an unjoined worker
Aborting user address space 29
```

The old matcher also required the intact `WORKER expected unjoined exit`
print, which was split in this capture. The control now requires the
runtime's unjoined-owner diagnostic, the same domain's kernel-abort
diagnostic and absence of an ordinary C exit. It no longer depends on the
redundant fixture print. The control's C fixture and runtime behavior are
unchanged; this does not make concurrent console records atomic.
The [failed result](../../plans/evidence/erts-memory-barriers/failed-unjoined-log/summary.json)
and [serial capture](../../plans/evidence/erts-memory-barriers/failed-unjoined-log/serial-excerpts.txt)
remain separate from the repeat.

## Remaining integration

The standalone data-barrier surface is implemented for the qualified
AArch64 C profile. The finite publication exercises and instruction checks
are not an exhaustive memory-model proof or a JIT qualification.
Full initialization, double-word atomics, fatal/assert/runtime support,
actual stack boundaries, thread progress and upstream build integration
remain open. Configure still rejects its missing thread library.

The earlier
[combined-campaign storage fragmentation finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains open. The barrier repeat uses a separate fresh guest.
