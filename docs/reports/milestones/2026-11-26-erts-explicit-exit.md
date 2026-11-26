# ERTS explicit worker exit — 2026-11-26

The [explicit exit adapter](../../../ports/erts/README.md#explicit-thread-exit)
stores a worker result and enters the native terminal exit path.
Callback return uses the same path. The runtime marks the worker as
exiting before TSD callbacks, preserves its participant identity through
cleanup, then removes that identity before the terminal thread syscall.
The exact native owner and C record remain retained through join or drain.

The [C exit contract](../../../ports/libc/README.md#explicit-worker-exit)
requires no live Rust owner or guard needing stack unwinding above this
terminal boundary. The native worker trampoline retains only Copy start
metadata there. Startup exit and recursive exit from a destructor abort
the domain. ERTS-specific exit handlers still need backend wiring.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 29 signed cases and three
rejection controls. The explicit-exit fixture exited through a nested C
call and delivered both a pointer and null result across two generations
of the same worker slot. Each exit ran a TSD destructor that observed the
worker identity and released its event before join completed.

A detached generation explicitly exited and settled through the adapter's
shutdown drain. The fixture destroyed its gate, deleted its key and
exited with zero remaining libc allocations.

The recursive fixture called explicit exit from its own TSD destructor.
The runtime emitted `recursive worker exit` and aborted the domain.
A subsequent signed C application completed. The same campaign reran
normal worker return, synchronization, TSD, event, thread and name fixtures.

The host Rust suite passed 77 unit tests and one compile-fail doctest.
Rust formatting, Python syntax and diff checks passed. The
[run summary](../../plans/evidence/erts-explicit-exit/summary.json)
identifies both exit fixtures, runtime inputs and signed artifacts.
The [serial excerpts](../../plans/evidence/erts-explicit-exit/serial-excerpts.txt)
retain successful exit and the recursive-exit diagnostic.

## Remaining integration

The adapter supports explicit exit from its workers. Full ERTS needs
exit-handler and thread-progress cleanup before that terminal boundary,
broader thread capacity, stack contracts, lifecycle wiring and background
detached-thread reaping. Direct startup exit was outside this fixture.

Repeat the qualification with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/exit-validation
```
