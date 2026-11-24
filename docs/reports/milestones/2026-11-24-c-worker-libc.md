# Picolibc worker profile — 2026-11-24

The read/write lock work was committed and pushed as
`d4119148a6e5b6e3d5f2ec4d7eb2054ae50771a7` to
`origin/feature/erlang-runtime`. This increment builds a separate,
opt-in Picolibc profile for a tested set of concurrent worker calls.
It leaves the worker fixture profile and the default single-thread
profile available. The build uses Picolibc 1.8.12 at
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. See the
[C runtime contract](../../../ports/libc/README.md#picolibc-profile-for-worker-calls).

## Implementation

The libc errno hook returns the entry for the current runtime participant.
The worker trampoline clears its entry before invoking C. Startup confirms
exit of the previous thread generation before reusing its slot. C keeps
any pointer derived from errno within the participant that received it.

The fixed stdin and shared stdout/stderr streams use statically installed
locks. The Rust bridge recognizes recursive acquisition by exact
participant and waits through the existing private CQ when contended.
Picolibc supplies no status channel for lock acquisition. The runtime
aborts on wait failure or after a five-second acquisition budget. The
terminal path validates joins before C finalizers or heap disposal.

The pinned CMake port omits the lazy stream initializer from its source
list and lacks the stdio-locking configuration macro. The target
toolchain supplies the macro, and the adapter rejects the lazy initializer
explicitly. The retarget hooks reject dynamic lock creation and stream
close. Standard formatting into a private buffer uses Picolibc's local
stream with its existing lock opt-out. No upstream source was patched.

## Scope of the AtomVM link

The `--worker-libc` option for the AtomVM build preserves `AVM_NO_SMP`.
It exercises the same Erlang semantics fixture through the new libc
closure while the dedicated C fixture makes concurrent worker calls.
The link map includes Picolibc mutable environment and timezone state
from `environ`, `_findenv`, `setenv` and `tzvars`. The pinned AtomVM
`localtime` hook calls `sys_time` before touching this state, and the
Knytos adapter aborts on that wall-clock call. The single-scheduler
fixture establishes compatibility for its executed closure. Stateful
environment and timezone routines remain outside the concurrent
qualification.

## Native results

All 18 signed AArch64 cases and three build or signature rejection
controls passed under QEMU 11.1.3 with four guest CPUs. The worker libc
fixture ran startup on LP 0 and a worker on LP 1. The worker's failed
allocation and overflowing conversion produced its own `ENOMEM` and
`ERANGE`. Startup retained `EDOM`. A new worker in the same roster slot
started with errno zero. The fixture verified complete output records
from both participants, including a record written while startup held a
recursive stream acquisition. Every allocated buffer was freed.

The fixed-stream close fixture aborted as expected. A separately signed
survivor launched after the failure. The single-scheduler AtomVM
semantics package passed on the same guest with the worker libc linked.
The signed worker libc fixture was 132,656 bytes. The signed AtomVM
fixture was 742,824 bytes. Neither signed fixture defined a global
`errno` symbol. The build validated the AArch64 ELF layout and absence
of ELF TLS. See the [artifact manifest](../../plans/evidence/erlang-worker-libc/summary.json),
[serial capture](../../plans/evidence/erlang-worker-libc/run-1/serial.txt)
and [writable libc link-map entries](../../plans/evidence/erlang-worker-libc/atomvm-libc-writable-symbols.txt).

## Reproduce

```sh
python3 ports/atomvm/build.py --atomvm-source /path/to/AtomVM \
  --otp-source .dependencies/otp-27 --picolibc-source /path/to/picolibc \
  --application ports/atomvm/tests/semantics.erl --otp-behaviours \
  --worker-libc --output target/atomvm/atomvm-worker-libc.elf
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --atomvm-semantics-elf target/atomvm/atomvm-worker-libc.elf \
  --smp 4 --output target/c-runtime/worker-libc-validation
cargo clippy --locked -p knytos-cabi --release --features worker-libc \
  --target crates/knytos-services/aarch64-unknown-none.json \
  -Z build-std=core,alloc,compiler_builtins -- -D warnings
cargo fmt --all -- --check
```

89 host Rust tests, 30 boot and CI input checks, target Clippy with
warnings denied and workspace formatting passed. Compile preparation
first found that the header exposes `flockfile` and `funlockfile` only
with POSIX declarations enabled. The fixture defines the declaration
feature macro. The next link detected the missing lazy initializer and
the profile added its explicit rejection hook.

## Remaining integration

The profile qualifies fixed-stream locking, participant errno and
the exercised allocation, conversion and formatting calls. It leaves
generic dynamic streams, full concurrent VM libc coverage, scheduler
worker publication, and transfer of the elected reactor role open.
The runtime's worker profile continues to use the existing kernel ABI
and private queues. See the [multicore plan](../../plans/erlang-m3-multicore.md).
