# AtomVM on Knytos

M2 runs the AtomVM 0.6.6 interpreter as an ordinary signed AArch64 EL0
application over the [C runtime](../libc/README.md). The platform adapter has
one scheduler and one native thread. Its immutable AVM package is compiled
into the executable's read-only LOAD memory and covered by its artifact
signature. There is no external module directory or package download.

## Reproduce the build

Use the repository Rust toolchain, Clang, CMake, Ninja, gperf, zlib development
files and the usual OTP host-build prerequisites. Acquire these exact inputs:

| Source | Release | Commit |
| --- | --- | --- |
| AtomVM | v0.6.6 | `ff993a80963298b532c1e573f883951ecaac9fef` |
| Erlang/OTP host compiler | OTP-27.3.4.19 | `fc7e10af595f3ffd2c25c84c253a4d042734cfed` |
| Picolibc | 1.8.12 | `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff` |

For example, clone OTP into `.dependencies/otp-27`, verify the commit, and
build it there with:

```sh
./otp_build autoconf
./configure --without-javac --without-wx --without-ssl --without-odbc \
  --disable-jit --without-debugger --without-observer --without-et --without-megaco
make -j6
```

OTP is a host tool in this milestone. This does not port ERTS to Knytos.
The installed system's OTP 29 compiler is unsuitable for this pinned AtomVM.
The adapter invokes `bin/erlc` from the explicitly supplied OTP source build
and checks the source pin, patch version and running OTP major version.

From the Knytos checkout, using clean pinned AtomVM and Picolibc sources:

```sh
python3 ports/atomvm/build.py --atomvm-source /path/to/AtomVM \
  --otp-source .dependencies/otp-27 --picolibc-source /path/to/picolibc \
  --output target/atomvm/atomvm-hello.elf
```

The build compiles the upstream core source list and this platform adapter,
generates upstream BIF/NIF lookup tables with gperf, and uses upstream PackBEAM
to package `hello` plus an explicit eight-module library closure. It builds a
host reference interpreter from the same clean AtomVM release. The source
remains unmodified. POSIX OS feature macros are absent from the target build;
`_POSIX_C_SOURCE` exposes libc's string-only `strdup` declaration, without
enabling native filesystem, socket or process support.

There are no VM-semantics patches, JIT, precompiled-native mode or dynamic NIF
loader in this profile. This release's interpreter is built directly rather
than assuming newer upstream configuration switches exist. Generated outputs
include unsigned ELF, `.build.json`, `.atomvm.json`, an AVM package, its byte
array, host tools, compiler stack-usage files and license notices. Retain
AtomVM and Picolibc notices with distributions and consult the pinned source
for individual file notices. Signing remains an explicit separate step.

## Platform behavior

The adapter uses the C runtime allocator and console and implements the pinned
release's actual `sys.h` hooks. `atomvm:platform/0` is registered as a static
native function returning `knytos`. Module lookup only borrows the signed,
embedded package. Package metadata and VM objects use the same tracked C heap;
the package bytes have executable-image lifetime and are never freed as heap.

`sys_poll_events` borrows launch CQ zero through the C runtime's checked wait.
It is the sole CQ consumer. A zero timeout returns immediately; finite waits
use the kernel watchdog and indefinite waits use readiness alone. The VM
retains its absolute timer expiry and rechecks runnable processes after wakes.
Conversion rounds remaining fractional milliseconds up to avoid a busy loop.
Waiter rejection, invalid waits and shutdown are explicit failures and abort
the domain; they cannot become a successful deadline or replay permission.
No asynchronous service operation or closeable C resource is exposed.

The pinned no-SMP scheduler does not require `sys_signal`; its native worker
and task-driver paths are disabled. The generic C event-signal operation
exercises a real kernel CQ generation posted before waiting. Queued Erlang
messages and a timed receive provide separate interpreter coverage. This
does not establish SMP wake or cross-thread notification semantics.

Monotonic time uses checked native counter conversion. This profile has no
authorized UTC client. Since upstream `sys_time` has no error return, wall
clock calls emit `unsupported: wall clock unavailable` and abort explicitly.
They never substitute uptime. A reviewed time-service/quality policy is
required before enabling them. File/package operations return unsupported
or failure, port creation fails, and listener/select operations abort with
an explicit diagnostic. They are not successful compatibility stubs.

Normal `ok` return destroys the context/global runtime and returns zero through
the C termination path. Other values return failure. C terminal cleanup frees
any remaining local heap blocks; this is distinct from kernel retirement and
does not qualify remote cancellation or a future Erlang resource finalizer.

## Execute validation

Build the explicit unavailable-UTC fixture too:

```sh
python3 ports/atomvm/build.py --atomvm-source /path/to/AtomVM \
  --otp-source .dependencies/otp-27 --picolibc-source /path/to/picolibc \
  --application ports/atomvm/tests/utc_unavailable.erl \
  --output target/atomvm/atomvm-utc.elf
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --atomvm-elf target/atomvm/atomvm-hello.elf \
  --atomvm-utc-elf target/atomvm/atomvm-utc.elf \
  --output target/atomvm/native-validation
```

The harness uses normal signed descriptors and TLS-authorized deployment,
retains C regression fixtures and launches a survivor after the negative VM
case. The VM descriptors permit one thread and 64 stack pages (256 KiB), within
the existing hard limit. This is an initial ceiling, not a measured minimum.
Heap remains limited by the existing 4 MiB profile. Stack samples are lower
bounds; full high-water and a production VM stack budget remain unqualified.

Run the same package on the reference interpreter with
`target/atomvm/atomvm-hello.atomvm-build/host/src/AtomVM` and
`target/atomvm/atomvm-hello.atomvm-build/application.avm`. It should print
`generic_unix` rather than `knytos`, hello, a receive timeout of at least 25 ms,
and `Return value: ok`. The native harness requires the Knytos identity.

## Erlang semantics and worker recovery

To build the semantic fixture, pass
`--application ports/atomvm/tests/semantics.erl --otp-behaviours` to
`build.py` and write it to `target/atomvm/atomvm-semantics.elf`. The flag adds
the pinned `erlang` wrappers, `gen_server` and `supervisor` to the embedded
package. Build a second image with
`--application ports/atomvm/tests/deadline_overflow.erl` and write it to
`target/atomvm/atomvm-overflow.elf`. This fixture requests a duration whose
nanosecond conversion fits but whose absolute expiry would overflow. The
adapter aborts with `unsupported: timer deadline overflow` before scheduling
that timer.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --atomvm-elf target/atomvm/atomvm-hello.elf \
  --atomvm-utc-elf target/atomvm/atomvm-utc.elf \
  --atomvm-semantics-elf target/atomvm/atomvm-semantics.elf \
  --atomvm-overflow-elf target/atomvm/atomvm-overflow.elf \
  --smp 4 --output target/atomvm/semantic-validation
```

The semantic fixture verifies Erlang values, garbage collection, process
links and monitors, selective receive, timer cancellation by message and
`one_for_one` worker recovery. Run the same package on the host reference
interpreter in `atomvm-semantics.atomvm-build/host/src/AtomVM`. To compare
against OTP, compile and run only `semantics.erl` using the pinned host OTP's
own library modules. Both comparisons expect all four stage markers,
`{m3,pass}`, and an `ok` result. The fixture produces expected crash reports
for two Erlang processes. The supervision check requires the replacement
worker to serve a request within the same native domain.

The signed VM descriptor permits one native thread. The runner's `--smp` flag
sets the guest CPU count. AtomVM SMP, TLS, general native-service NIFs, external
networking, ERTS and multi-node distribution require further work. See the
[remaining scheduler requirements](../../docs/plans/erlang-m3-multicore.md).
