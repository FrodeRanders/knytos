# AtomVM on Knytos

M2 runs the AtomVM 0.6.6 interpreter as an ordinary signed AArch64 EL0
application over the [C runtime](../libc/README.md). The default platform
adapter has one scheduler and one native thread. Its immutable AVM package is compiled
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

## Default platform behavior

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
Waiter rejection and invalid waits abort the domain. An authenticated
shutdown request starts the scheduler stop and C cleanup path.
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

The semantic fixture's descriptor permits one native thread. The runner's
`--smp` flag sets the guest CPU count. TLS, general native-service NIFs,
external networking, ERTS and multi-node distribution require further work.

Pass `--worker-libc` to the AtomVM builder to link the separate Picolibc
worker profile. Use the same semantics fixture and
`--atomvm-semantics-elf` runner argument to verify that linked closure.
This build still uses `AVM_NO_SMP`. The profile's concurrent C fixture
qualifies fixed streams and errno independently of the VM.

## Fixed native schedulers

Pass `--schedulers 2` or `--schedulers 4` to enable AtomVM SMP and the
worker libc profile. Startup creates all worker threads before entering
the VM. Each thread parks on its private CQ. When AtomVM reserves another
scheduler, the adapter releases a prepared worker after the ready-list
and scheduler locks have been released. The configured roster is fixed.
Changing `schedulers_online` through `erlang:system_flag/2` returns
`badarg`.

The elected AtomVM poller borrows CQ zero. While it scans for work,
`sys_signal` coalesces hints into one pending notification. The poller
consumes a pending hint by rescanning before it parks. The first hint
after it arms advances the CQ generation, which covers a wake before
the wait enters the kernel. The Rust adapter rejects overlapping waits
on its shared cursor. The kernel posts its lifecycle wake directly
to CQ zero. Other schedulers use event-only condition waits on their
private CQs. The runtime registers a waiter before releasing its mutex
and returns success after reacquiring it. Lock acquisition and condition
reacquisition each have a five-second budget. Any failed wait or
acquisition aborts the domain.

For a read, the adapter first tries shared acquisition. A busy result
enters the bounded wait under the same writer admission rules.

During idle operation the poller observes shutdown requests on CQ zero.
When the ready queue keeps the VM out of CQ waits, the elected scheduler
observes lifecycle at its nonblocking scheduling checkpoint. It requests
the VM's scheduler stop while private synchronization continues to serve
peers. The scheduler rechecks the stop before returning selected work to
the interpreter. Startup wakes unused workers and joins every exact native
worker. The current profile owns a local C allocation graph, which the
runtime releases with its terminal heap cleanup before acknowledging.
Erlang process exit callbacks are outside this retirement contract.

The build generates target-only adaptations from the clean pinned source.
The SMP NIF lookup excludes dynamic package add/close and `read_priv/2`.
The embedded package remains signed and the worker profile still aborts
when the VM calls the unavailable wall-clock hook. This excludes the
mutable environment and timezone paths behind that hook.

Build the variants in sequence because they share one Picolibc build cache.

```sh
for count in 1 2 4; do
  python3 ports/atomvm/build.py --atomvm-source /path/to/AtomVM \
    --otp-source .dependencies/otp-27 --picolibc-source /path/to/picolibc \
    --application ports/atomvm/tests/schedulers.erl --schedulers "$count" \
    --output "target/atomvm/schedulers-$count.elf"
done
python3 ports/atomvm/build.py --atomvm-source /path/to/AtomVM \
  --otp-source .dependencies/otp-27 --picolibc-source /path/to/picolibc \
  --schedulers 4 --output target/atomvm/schedulers-idle-4.elf
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --atomvm-scheduler-elf target/atomvm/schedulers-1.elf \
  --atomvm-scheduler-elf target/atomvm/schedulers-2.elf \
  --atomvm-scheduler-elf target/atomvm/schedulers-4.elf \
  --atomvm-idle-scheduler-elf target/atomvm/schedulers-idle-4.elf \
  --smp 4 --output target/atomvm/scheduler-validation
```

The runner checks the adjacent build manifest and signs each descriptor
with the configured thread count and 64 stack pages per thread. The
workload checks the native scheduler entry markers, two rounds of Erlang
message exchange, idle timer deadlines and exact worker joins. The idle
fixture runs the hello package and checks joins for workers that never
entered the interpreter. See the
[remaining scheduler requirements](../../docs/plans/erlang-m3-multicore.md).

## Signed deployment retirement

Build `tests/shutdown_busy.erl` with one and four schedulers and
`tests/shutdown_parked.erl` with four. Use the same pinned source arguments
as above and set `--application` to the fixture path. Give the outputs to
the signed runner on a four-CPU guest.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --atomvm-shutdown-parked-elf target/atomvm/shutdown-parked-4.elf \
  --atomvm-shutdown-busy-elf target/atomvm/shutdown-busy-1.elf \
  --atomvm-shutdown-busy-elf target/atomvm/shutdown-busy-4.elf \
  --smp 4 --output target/atomvm/retirement-validation
```

After the original VM reaches its workload marker, the runner submits one
signed replacement of that deployment with sequence two. It checks the
original domain's shutdown observation, exact joins, local heap disposal
and supervisor acknowledgement before the succeeding generation's workload
marker. The signed grace period is five seconds. Future NIFs that own
external resources require separate qualification.

## Signed whole-node shutdown

Add `--atomvm-node-shutdown` to keep the supplied retirement fixtures
alive for a single node shutdown. For example, use the four-scheduler
busy and parked images on the same guest.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --atomvm-shutdown-parked-elf target/atomvm/shutdown-parked-4.elf \
  --atomvm-shutdown-busy-elf target/atomvm/shutdown-busy-4.elf \
  --atomvm-node-shutdown --smp 4 --output target/atomvm/node-shutdown-validation
```

The runner waits for both workloads, then submits a signed node shutdown
intent over the management deployment ingress. It holds idle client
connections on both management ingresses and checks their drain. It
verifies each VM's join and heap cleanup before the supervisor's
acknowledgement, then checks the deployment agent's cooperative exit,
the PSCI marker and a guest-originated QMP shutdown with QEMU exit status
zero.

## Scheduler performance

Build `ports/atomvm/tests/performance.erl` with the same pinned inputs
and `--schedulers 1`, `2` and `4`. Then pass the three images to the
signed runner.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --atomvm-performance-elf target/atomvm/performance-1.elf \
  --atomvm-performance-elf target/atomvm/performance-2.elf \
  --atomvm-performance-elf target/atomvm/performance-4.elf \
  --smp 4 --output target/atomvm/performance-four-cpus
```

The fixture warms up 12 client and echo pairs, then times three batches
of 96,000 request and reply messages. It samples 32 ten-millisecond
receive deadlines and 32 send-to-receive latencies after quiet intervals.
It then runs an eight-second idle window and at least eight seconds of
repeated message batches. The runner takes two authenticated
`/metrics` snapshots in each window. It checks the guest counter time,
the exact thread generations and blocked states during idle, then
records CPU tick deltas and the kernel's heap and stack counters.
Run one image per guest and set `--smp` to its scheduler count to
measure configurations with matching guest and scheduler counts.
On normal SMP exit, `kernel_wakes` counts CQ zero wake calls from
the VM's elected-poller signal path.
