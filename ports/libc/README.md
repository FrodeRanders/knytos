# Static C applications on Knytos

This is the M1 AArch64 C11 profile: one native thread, no ELF TLS, and a
statically linked Picolibc subset over the existing Rust runtime. It uses
kernel runtime ABI 4 and C API version 1. AtomVM integration is separate.

## Build

Install Clang, CMake, Ninja and the repository's pinned Rust toolchain with
its LLVM tools. From the repository root:

```sh
python3 ports/libc/build.py ports/libc/tests/suite.c \
  --fetch --output target/c-runtime/hello.elf
```

`--fetch` clones Picolibc into `.dependencies/picolibc` if absent. Alternatively
pass `--picolibc-source /path/to/picolibc`. The source must be clean at 1.8.12,
commit `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. The build uses single-thread
mode, global errno, no TLS, and the Rust allocator in place of libc malloc.
The selected libc functions and Rust compiler builtins link into one ELF;
there is no `sbrk`, dynamic linker or second heap.

The output is unsigned. Its sibling `.build.json` records compiler versions,
source/input hashes and the unsigned ELF hash. `picolibc-COPYING.txt` contains
the upstream license inventory; retain it with distributed artifacts and
consult the pinned source for individual file notices. No upstream source
patches are required. Signing changes the ELF hash.

Use the normal artifact signing and authenticated deployment process. The
test harness demonstrates `cluster-sign elf-sign`, `deployment-sign` and
`deployment-notify`; its development fixture key is not a production key.
The kernel independently checks signatures, ABI, mappings and signed resource
limits. Build checks reject TLS, runtime relocation sections, dynamic program
headers and invalid or writable/executable LOAD layouts.

## Runtime contract

The Rust `_start` validates the launch context and initializes the standard
heap status record. Loader-provided data and BSS are retained. Constructors
run in ascending priority before `main(1, argv)`, with `argv[0] = "knytos-c"`
and a terminating null pointer. There is no environment or argument parser.
Constructor boundaries may coincide when no constructors are linked.

`malloc` and `calloc` return independently freeable blocks even for zero size.
`free(NULL)` does nothing. `realloc(p, 0)` frees `p` and returns null; other
failed reallocations preserve `p`. Arithmetic overflow and allocator exhaustion
return null with `ENOMEM`. `aligned_alloc` requires a nonzero power-of-two
alignment and a size divisible by it, otherwise `EINVAL`; successful realloc
preserves the original alignment. Only live pointers from this allocator may
be freed or resized, exactly once. The private list retains each allocation's
original Rust layout until release. It owns heap bytes, not capabilities.

`stdout` and `stderr` share a bounded console buffer, flushed on newline,
capacity or explicit flush into length-delimited Knytos log records. There
is no terminal or file descriptor namespace. `stdin` returns EOF with `ENOSYS`.
String, conversion, formatting and math functions come from Picolibc;
unsupported OS imports fail to link rather than return fabricated success.

`include/knytos.h` defines fixed-width statuses and C layouts for API version
1. Buffers must be valid in this native domain for the call's duration.
Output pointers must have their C type's alignment. Null pointers, oversized
console writes (over 65,536 bytes), and clock conversion errors return explicit
status. Monotonic time uses checked counter/frequency conversion. Realtime
always returns `KNYTOS_UNAVAILABLE` without changing the output: this profile
does not connect to the authorized time service. Uptime is never Unix time.

The single-threaded reactor API borrows launch CQ zero: `knytos_event_signal`
posts a retained work generation, and `knytos_event_wait` accepts -1 for an
indefinite wait or nonnegative milliseconds. It returns distinct readiness,
deadline, waiter rejection, invalid request and authenticated shutdown statuses.
It checks lifecycle before and after waiting. The caller must be the sole CQ
consumer, retain any accepted operation across all outcomes, and carry its
own absolute deadline across unrelated readiness. No capability is adopted or
result observed by this wait. The [AtomVM adapter](../atomvm/README.md) uses
it with no outstanding native service operations and aborts on wait errors.

Returning from `main` calls `exit`. Exit callbacks registered with `atexit`
run before destructors, then destructors run in reverse priority. Rust flushes
output, releases all remaining C heap blocks and logs the C result, released
block count, remaining count, runtime heap peak and sampled stack depth.
Zero exits the sole thread; nonzero records the C status and aborts the domain.
There is no kernel C process exit-status API. `_Exit` skips C callbacks while
performing terminal heap cleanup; `abort` skips normal exit and invokes domain
abort. Recursive exit aborts. Kernel retirement remains responsible for the
domain; local heap release is not a claim of domain retirement completion.

There are no C resource tokens, workers or asynchronous operations in this
profile. It borrows the launch context and does not adopt launch-owned
capabilities. Extending it with IPC requires actual Rust owners, consuming
transfer and confirmed completion/cancellation before release. Do not add
integer capability ownership or blocking cleanup to libc finalizers.

## Validate

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --output target/c-runtime/native-validation
```

The output directory must be new. The harness builds bundled AArch64 Rust
services with the standard runner, creates a fresh disk, and launches signed
C fixtures through TLS-authorized deployment. It uses localhost ports 28080
and 28081. It waits for boot, time and controller readiness before submission;
it does not retry accepted deployment mutations. Results correlate launch
ASIDs with output, and failed campaigns retain separate diagnostics. QEMU
needs permission to open the local ports and diagnostic sockets.

Fixtures cover ABI calls, allocation pressure, formatting/math, clocks,
constructor/exit ordering, deliberate leaked heap cleanup, nonzero return,
abort, empty callback arrays and a successful launch after failures. TLS and
unsupported imports must fail to build; tampering must fail signature checks.
Each descriptor permits one thread and 16 stack pages (64 KiB). Stack depth
sampled at selected Rust entry points is a lower bound, not a complete stack
high-water measurement or a VM stack budget. Heap pressure intentionally
approaches the existing 4 MiB heap limit; it does not establish a VM minimum.

TLS models, pthreads, SMP, POSIX processes/filesystems, general C IPC and JIT
execution are unsupported. These restrictions must be requalified before a
native VM or additional threads use this runtime.
