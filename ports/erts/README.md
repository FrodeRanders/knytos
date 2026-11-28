# ERTS platform prerequisites

The port targets OTP 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`. The
[programme plan](../../docs/plans/erlang-runtime-port.md#12-milestone-m5--official-erts-port)
tracks the remaining threading, allocator, I/O and bootstrap work.

## Configure probe

`configure.py` exports the pinned OTP source into a new directory, adapts
target tuple recognition and runs its configure script against the
Knytos worker-libc SDK. See the [configure results](../../docs/reports/milestones/2026-11-26-erts-target-configure.md)
for the original arguments and pthread-library rejection. The default probe
retains that behavior. The explicit native probe now passes the thread gate
and stops at OTP's wall-clock-source requirement:

```sh
python3 ports/erts/configure.py --native-ethread \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/native-configure
```

This mode builds and checks the native archive, stages the selected OTP
headers and adds `--with-knytos-ethread=../sdk --without-termcap` to configure.
Headless execution uses OTP's supported termcap opt-out. It regenerates
configure from the patched pinned sources with host Autoconf and records
that version and generated-script hash. No cached POSIX feature answers
are supplied.

`knytos-ethread.m4` uses real target size checks and compile-links all 327
retained public/native API references. The captured ELF passes the C SDK's
static AArch64 layout checks. Omitting the native archive must fail linkage,
and selecting pthreads alongside Knytos must fail compilation. Configure
reports native atomics only after the selected headers enforce lock-free
widths and the actual link succeeds; native spinlock optimization is absent.

The versioned native configure patch also routes the ethread Make source
list to the nine native sources. GNU Make checks that branch and preserves
the original pthread list. Compiling the staged sources with the qualified
SDK reproduces the same native archive. These checks do not build the full
OTP Make graph or produce an emulator: thread-only configure exits 1 with
`No wall clock source found` before generating its final outputs. This
configure SDK uses the worker-libc profile, which reports realtime unavailable.
The [service-worker C profile](../libc/README.md#authorized-realtime-queries)
can query UTC through a signed time grant. The standalone clock prerequisites
below provide local getters. The explicit clock mode described next selects
their configure interface. Native-platform mode below additionally selects the
common-header path and clock sources; full ERTS startup remains open.
The [native configure qualification](../../docs/reports/milestones/2026-11-27-erts-native-configure.md)
records the retained references, two rejection controls, Make source checks
and byte-identical staged archive. Its
[evidence manifest](../../docs/plans/evidence/erts-native-configure/summary.json)
preserves the default thread gate, initial curses stop and final clock gate.

### Native clock configure selection

```sh
python3 ports/erts/configure.py --native-ethread --native-clock \
  --clock-sample-age-ms 5000 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/clock-configure
```

Use a fresh output directory. This opt-in mode requires native ethread and an
explicit sample age of 1..2147483647 milliseconds. It builds the service-worker
C SDK and the three-member clock archive, then adds
`--with-knytos-clock=../sdk --with-knytos-clock-max-age-ms=5000` to configure.
The chosen age is a finite qualification window; no refresh producer is
installed. Startup still requires a signed `time=call` grant.

`knytos-clock.m4` compile-links twelve retained clock/startup/performance-counter
interfaces against the actual pinned time-record contract and checks the
SDK's policy and service-worker profile. This selects native clock definitions
without successful POSIX clock, corrected-time or CPU-accounting answers.
Archive omission, signature/policy/profile mismatches and invalid native
target/backend/age/POSIX choices reject. The probe supplies `-Os` to satisfy
OTP's explicit optimization requirement, consistent with adapter builds.

Configure exits zero and emits its headers and Makefiles. Generated definitions
and native ethread source selection are checked. This does not qualify the
full Make graph: common `sys.h` still selects `erl_unix_sys.h`, the emulator
Makefile still selects Unix sources including `sys_time.o`, and the probe's
relative SDK paths still need propagation through the actual build layout.
Native platform source/header routing, startup/refresh ownership and a matching
host bootstrap remain integration work. See the
[qualification](../../docs/reports/milestones/2026-11-28-erts-clock-configure.md).

### Native platform header and clock Make routing

```sh
python3 ports/erts/configure.py --native-ethread --native-clock --native-platform \
  --clock-sample-age-ms 5000 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/native-platform
python3 ports/libc/run-tests.py --erts-time-init \
  --erts-platform-build target/erts/native-platform --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/native-platform-validation
```

This additional opt-in selects `ERTS_KNYTOS`, `ERLANG_OSTYPE=knytos` and
`OPSYS=knytos`. Common upstream `sys.h` includes `erl_knytos_sys.h`, which
provides standard C prerequisites, the existing native clocks and a scalar
remaining-duration record. It imports no Unix sys header or descriptor/signal
backend. Unsupported native operations retain upstream declarations without
success stubs. Conflicting platforms, a missing service-worker profile,
the extracted-record probe and descriptor-readiness requests reject.

The generated emulator Makefile selects the three clock objects instead of
Unix platform objects. The qualifier stages generated configuration into OTP's
in-tree layout and builds those objects with the actual Make rules. SDK paths
are absolute in this mode so the compiler works from the emulator directory.
GNU libc declaration visibility exposes implemented routines such as `memrchr`;
it does not supply feature-test answers or implement POSIX services.

Clock-only qualification uses OTP's `ERTS_SKIP_DEPEND=true` and marks the
unneeded BEAM generator and Git-version include as old through Make's `-o`.
It creates no substitute generated headers. The full dependency/generation
graph remains unqualified. Only the logging template's default verbosity is
rendered outside ERTS configure. Unix and Win32 source lists are preserved.

The linked consumer includes complete pinned `sys.h`, `erl_threads.h`, native
`ethread.h` and generated configuration, with dependency checks excluding the
Unix and extracted time-record paths. It checks all fourteen time-record
fields and twelve clock interfaces. The guest campaign uses this Make-built
archive and complete headers for startup/worker reads and terminal controls.
OTP's configured stack-protector flags are retained; the campaign checks the
Picolibc constructor initialized its guard, without claiming secret entropy.
See the [qualification](../../docs/reports/milestones/2026-11-28-erts-platform-routing.md).

Native ERTS startup, refresh ownership, I/O, CPU accounting, whole-emulator
dependencies and matching host bootstrap remain open. The optional mode below
covers generated emulator metadata. The sample
window is still finite and installs no refresh producer. This is not an
emulator boot or a complete platform implementation.

### Generated emulator metadata

Add `--native-metadata` to the native-platform configure command to build the
pinned host YCF source generator and run the actual complete `GENERATED`
target before compiling clocks:

```sh
python3 ports/erts/configure.py --native-ethread --native-clock --native-platform \
  --native-metadata --clock-sample-age-ms 5000 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/native-metadata
```

The host tool uses the exported pinned OTP sources and their upstream
standalone Makefile. Build/test inputs are checked against pinned Git blob
identities before compilation. The host test environment selects this export's
wrapper and executable ahead of installed tools. The upstream YCF test suite
must pass. This is a pinned source generator, not a complete host Erlang
bootstrap or a compiler for preloaded BEAM modules.

The native Make dependency for each YCF transform is that actual host
executable. Other platforms retain the target-library dependency. Interpreter
opcode generation explicitly uses full-width code pointers (`large`) instead
of an unexpanded code-model placeholder; this does not enable JIT or change
the native compiler's code model. Upstream Perl/YCF rules generate opcode,
atom, BIF, allocator, version, driver and yielding-C metadata, including the
real `GENERATED` marker. This mode builds clocks without marking that target
old. Whole-emulator dependency scanning and the optional Git-version include
remain outside this bounded qualification.

Unchanged upstream `erl_term.c` and `erl_init.c` compile using these generated
headers. The native profile permits upstream's `+A0` lower bound; this creates
no async pool and does not qualify positive pool sizes. The preflight retains
the common time-supervisor's remaining CPU-accounting/calendar diagnostics
rather than inventing those implementations. Missing metadata, changed host source and missing generator
controls reject. See the [qualification](../../docs/reports/milestones/2026-11-28-erts-generated-metadata.md).

### Native UTC calendar and common time supervisor

Add `--native-time` after `--native-metadata` to compile the common time
supervisor and BIF source with native calendar routing:

```sh
python3 ports/erts/configure.py --native-ethread --native-clock --native-platform \
  --native-metadata --native-time --clock-sample-age-ms 5000 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/native-time
python3 ports/libc/run-tests.py --erts-time-init \
  --erts-platform-build target/erts/native-time --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/native-time-validation
```

This headless profile defines local calendar time as UTC, with no DST or
ambient `TZ` policy. The four current-date/time getters read the published
authorized clock and use reentrant `gmtime_r`; no RPC occurs in a getter.
Preparation failure leaves the output unchanged. Statusless upstream getters
diagnose and abort when the clock is unavailable, stale or out of range.
Validated local/universal conversions preserve their fields under UTC;
the pure upstream Gregorian conversions remain common code.

`statistics(runtime)` raises `notsup`: existing scheduler counters do not
provide a generation-preserving domain total with the required user/kernel
accounting. The unused CPU reader, declaration and mutex are excluded.
The test-only `proc_sig_block` hook also raises `notsup` until a native blocking
contract exists. Other platforms retain their original branches.

The generated Make rules build a fourth clock object for the calendar adapter.
Whole common time-supervisor and BIF compilation must pass. A separate archive
of the actual common time-supervisor object lets signed C fixtures execute its
calendar functions using section GC. This qualifies the selected calendar
surface; it does not execute the entire time supervisor or an Erlang BIF.
The [qualification](../../docs/reports/milestones/2026-11-28-erts-time-supervisor.md)
records host error tests, native Gregorian/current-clock tests and signed
denial/terminal controls. Full ERTS startup, continuous refresh, CPU accounting,
timer integration and emulator boot remain open.

## Native monotonic clock prerequisite

`erts_monotonic.c` implements OTP's `erts_os_monotonic_time` and
`erts_sys_hrtime` as local scalar reads through `knytos_clock_monotonic_ns`.
Both return signed nanoseconds. A source error or a value beyond the signed
64-bit range terminates the domain; these OTP interfaces have no error result.
Successful reads allocate no memory, issue no RPC and perform no wait. They
rely on the kernel's counter contract without claiming frequency correction.
The diagnostic path uses a bounded literal and domain abort, and is not
qualified for IRQ, signal or reentrant console contexts.

This independent archive uses the worker-libc profile and requires no time
grant. The native-clock configure probe selects its link interface; native
platform mode also selects it through complete common `sys.h` for clock builds:

```sh
python3 ports/erts/check-clock.py --otp-source /path/to/otp29 \
  --picolibc-source /path/to/picolibc --output target/erts/clock-interface
python3 ports/libc/run-tests.py --erts-clock --smp 2 \
  --picolibc-source /path/to/picolibc --output target/erts/clock-validation
```

Use fresh output paths. `build.py --monotonic-clock` builds only
`erts_monotonic.o`; the default nine-member ethread archive remains separate.
The checker extracts the pinned LP64 types and function declarations,
compile-links both retained references, rejects signature and unit mismatches,
checks archive dependencies and exercises both functions at their numeric and
error boundaries under host undefined-behavior sanitization. The signed fixture
reads both clocks under a native mutex on the startup thread and one worker.
See the [qualification](../../docs/reports/milestones/2026-11-27-erts-monotonic-clock.md).

Full-platform system-time integration remains open. OTP reads it inside `erts_get_time_mtx` and from
scheduler BIFs. The service-worker UTC query can block for its one-second
budget and can reject grant preparation as busy, so it cannot simply replace
that statusless hot-path getter. The C ABI provides
[caller-owned samples and bounded local projection](../libc/README.md#caller-owned-utc-samples-and-local-projection).
The blocking query supplies the service's UTC/counter anchor and drift; local
projection uses one scalar counter read with an explicit sample-age limit.
The standalone system-clock prerequisite below supplies publication, explicit
refresh, step adjustment and terminal expiry. Native-clock configure selection
is available above; startup/refresh scheduling still needs integration.
Full-platform `sys_init_time` and paired-clock integration,
performance-counter selection and timer-wheel integration remain unqualified.

## Published system-clock prerequisite

`erts_system_time.c` implements the pinned `erts_os_system_time` and
`erts_os_times` interfaces. `knytos_erts_clock_refresh` queries a signed UTC
sample outside clock-read locks, then attempts local publication with a
caller-chosen nonzero nominal sample-age limit. `knytos_erts_clock_publish`
can publish an already prepared immutable sample. It validates representation,
age and signed range; it does not authenticate caller-provided scalars.

Two static slots retain only copied non-owning data. A reader increments a
global pin count, selects the active slot, copies its sample/limit and releases
the pin before local projection. A publisher takes one admission flag and
requires zero pins before reusing the inactive slot. New readers can use the
prior active slot while preparation proceeds, then the completed replacement.
Sequentially consistent count/index operations order retirement and publication.
The bounded roster has startup plus at most three workers, with no reentrant
read callbacks, so the count cannot wrap. There is no allocation, RPC, native
mutex acquisition or explicit wait/retry on the read path. Native atomic
operations are lock-free; this is not a wait-free latency guarantee.

Busy publication returns `KNYTOS_BUSY` and preserves the active sample. Every
fallible query/validation step precedes slot mutation. Refresh does not retry
or extend an old anchor. The status-bearing `knytos_erts_clock_read` preserves
output on error; the statusless ERTS getter emits a bounded diagnostic and
aborts on missing time, expiry, counter error or signed-range exhaustion.
Each reader validates its captured sample even if a replacement has appeared.
Replacing the sample may step UTC in either direction; no global monotonic UTC
clamp is invented. Original uncertainty/NTP age remain metadata, not a UTC
accuracy or security guarantee. IRQ/signal/reentrant use remains unqualified.

The paired getter follows OTP's generic fallback: sequential local monotonic
and system reads. It is not an atomic single-counter pair. The archive remains
a standalone prerequisite. The startup adapter below supplies time metadata;
native ERTS startup and a refresh timer/notification owner remain open.
A runtime must arrange successful initialization before its first
clock read and refresh before its chosen validity policy expires.

```sh
python3 ports/erts/check-system-clock.py --otp-source /path/to/otp29 \
  --picolibc-source /path/to/picolibc --output target/erts/system-clock-interface
python3 ports/libc/run-tests.py --erts-system-clock --smp 2 \
  --picolibc-source /path/to/picolibc --output target/erts/system-clock-validation
```

Use fresh output directories. `build.py --system-clock` builds the two clock
objects with the service-worker SDK. The checker compares four actual pinned
clock declarations and three scalar types, rejects return/pointer mismatches,
links the full surface and checks archive dependencies. Nine host scenarios
include deterministic reader/publisher admission, backward steps, expiry,
failed refresh, signed bounds and slot reuse after copying. A four-thread
stress scenario checks 15,000
reads against 5,000 independent publication attempts under ThreadSanitizer.
The signed profile covers live replacement during worker reads, getters under
a native mutex, explicit refresh, grant denial, cold getter domain abort and
a later survivor. See the [qualification](../../docs/reports/milestones/2026-11-27-erts-system-clock.md).

## Startup time adapter

`erts_time_init.c` supplies the pinned `sys_init_time` and
`erts_late_sys_init_time` entry points. Its status-bearing
`knytos_erts_init_time` prepares one authorized UTC sample, validates its local
projection, prepares all metadata, then publishes the sample before committing
the caller's result. Query, validation or publication errors preserve that
entire result. The statusless OTP initializer emits a bounded diagnostic and
aborts on failure. A failed diagnostic still terminates.

The metadata reports an available, uncorrected monotonic source, signed
nanosecond units, unlocked/nonextended reads and actual native function names.
Resolution fields describe initial conservative numeric granularity from the
sampled counter frequency and drift, not UTC uncertainty or refresh rate.
Full-platform refresh must also qualify its descriptive-metadata policy if
granularity changes. The generic
timer-wheel unit remains one millisecond. `erts_clock_platform.h` maps the
performance counter to local high-resolution monotonic time with nanosecond
units; it supplies no CPU-accounting ticks or CPU-time implementation.

Source inspection finds the initializer through Unix `sys_init` calling
`erts_init_sys_time_sup`, before `erts_init_time_sup` creates the time
supervisor's locks. The complete native platform path is not built yet.
`erts_late_sys_init_time` needs no counter wrap extender and performs no
refresh. It never renews the original sample's age. Runtime refresh scheduling
still needs its own producer/owner before a long-running emulator can use this
backend.

```sh
python3 ports/erts/check-time-init.py --otp-source /path/to/otp29 \
  --picolibc-source /path/to/picolibc --output target/erts/time-init-interface
python3 ports/libc/run-tests.py --erts-time-init --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/time-init-validation
```

Use fresh paths. `build.py --platform-clock --otp-source ...` builds three
clock objects with the service-worker SDK and an explicit probe profile. Its
default `--clock-sample-age-ms=5000` is a five-second qualification window;
the generated compiler define is mandatory and nonzero. It is not a runtime
refresh policy. `stage_time.py` extracts the actual LP64 initialization record,
initializer and declarations from pinned OTP `sys.h`. The explicit
`KNYTOS_ERTS_TIME_CONTRACT_PROBE` flag selects that slice; without it the adapter
includes the real `sys.h`. This standalone profile does not qualify the complete
sys header or select sources in OTP Make. Native-clock configure uses the same
explicit record boundary; the additional native-platform mode above compiles
complete common `sys.h` and the generated clock Make rules.

The checker verifies all fourteen scalar field types/offsets and record
size/alignment, rejects a wrong field type and missing/zero age policies,
and links the native initializer. Nine host scenarios cover metadata,
granularity arithmetic, unchanged outputs, source/publication errors and
terminal startup failure. The signed profile exercises real initialization,
immutable metadata and local counters on startup/worker threads, missing-grant
denial, startup domain abort and a later survivor. See the
[qualification](../../docs/reports/milestones/2026-11-27-erts-time-init.md).

## Standalone archive

`build.py` compiles each adapter as a separate AArch64 object and packages
them as `libknytos_ethread.a`. It prepares the pinned worker-libc SDK,
uses the same freestanding C11 profile and emits an adjacent build manifest
with archive, member and source hashes. Use a fresh output path.

```sh
python3 ports/erts/build.py --picolibc-source /path/to/picolibc \
  --output target/erts/prerequisites/libknytos_ethread.a
```

Consumers include `knytos_ethread.h` and link the archive through
`ports/libc/build.py --include ports/erts --library /path/to/libknytos_ethread.a`.
The aggregate header exposes the standalone prerequisites. An explicit
staged OTP header selection and bounded native bootstrap are described below.
ERTS CPU/runtime data, clock-source configuration and the full build remain open.

### Pinned interface check

`check-interface.py` reads declarations directly from the pinned OTP Git
objects and checks the public `ethr_*` functions exposed by the aggregate header.
It compiles those declarations alongside the adapters, checks six scalar
typedefs against OTP's target-size branches, and verifies the nine portable
records' member order, field types, offsets, size and alignment. It also
checks the double-word value union, two option enums and thirteen public
event, name and ordering constants. Opaque native event, mutex, thread and
key representations remain platform-specific.

```sh
python3 ports/erts/check-interface.py --otp-source /path/to/otp29 \
  --picolibc-source /path/to/picolibc --output target/erts/interface
```

Use a fresh output directory. The check builds the archive, generates an
AArch64 consumer with retained address references to every covered function,
and links it through the worker-libc SDK. It verifies all references and
archive members in the link map. Two compile rejection controls change a
function return type and a portable field type; each must fail with the
corresponding diagnostic.

The checker also retains references to the 33 native callable names.
Scalar native names are source-level aliases to the existing full-barrier
functions; word-pair names are shared native implementations. Their
callability is checked separately from matching the public OTP prototypes.

The initial [recorded check](../../docs/plans/evidence/erts-interface/summary.json)
matched and linked 253 functions (184 inline atomic and 69 other functions),
all nine portable records, both enums and the constants. Both mismatch
controls rejected. The [generated consumer](../../docs/plans/evidence/erts-interface/contract.c)
and [diagnostics](../../docs/plans/evidence/erts-interface/diagnostics.txt)
retain the checked declarations and negative results.

This checks declaration slices against the current standalone surface.
It does not include OTP's complete `ethread.h`, execute the consumer,
select an upstream backend or qualify unavailable functions. Full ERTS
startup remains open; the native-clock configure mode above passes the clock gate.
Runtime behavior is covered by the signed campaigns below.

### Upstream header selection

`otp-29.1.2-ethread.patch` adds explicit `ETHR_KNYTOS` branches to OTP's
own `erts/include/internal/ethread.h`. OTP still supplies its scalar types,
thread options, allocator/init layouts, thread/TSD declarations, and the
public thread-event structure and flags. The selected branches use the
adapters' non-owning native identities, C11 atomics, events and mutexes.
`ethr_native_types.h` is shared with the standalone thread and TSD headers.

The selection keeps low-level locks and thread-event operations external,
including when `ETHR_TRY_INLINE_FUNCS` is requested. This preserves the
adapters' ownership and callback checks and prevents OTP's pthread-backed
inline implementations from replacing them. Conflicting pthread/Win32
selection and a missing worker-libc profile reject at compile time.
`knytos/ethr_extensions.h` exposes the existing bounded bootstrap and drain
helpers after the selected header; these remain private Knytos interfaces.

```sh
python3 ports/erts/check-interface.py --upstream-header \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/header-interface
```

`stage_headers.py` exports pinned Git objects to the new output directory,
applies the version-controlled patch without fuzz and copies adapter headers
under `internal/knytos/`. It leaves the source checkout untouched. The
explicit compile-probe profile takes integer sizes from Clang's target
builtins and uses OTP's existing 64-byte cache-line assumption. It is not a
generated Autoconf result. The interface checker verifies the selected
include path, compiles normal, inline and debug modes, links the covered
surface and checks four rejection controls.

The signed profile compiles lock, atomic, thread-event, allocator and
memory-barrier fixtures through that selected header, including debug
thread-event and barrier fixtures. It changes their aggregate include in generated
copies and retains the original test logic. Run it in a fresh guest:

```sh
python3 ports/libc/run-tests.py --erts-header --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/header-validation
```

The [header-selection qualification](../../docs/reports/milestones/2026-11-27-erts-header-selection.md)
records the selected paths, artifacts, modes and signed results. This closes
selection for the currently qualified adapter surface. Native `ethr_init`
and `ethr_late_init` are implemented for the bounded profile below; ERTS
CPU/runtime configuration remains open. Terminal diagnostics are also below.
The double-word and data-barrier interfaces are described below. No emulator
is built by this probe. Default configure retains its pthread rejection;
thread-only configure reaches the wall-clock gate, while the explicit
native-clock probe completes configure.

## Atomic operations

`ethr_atomics.h` provides the pinned interface's 32-bit and word-sized
atomic families for the AArch64 C11 profile. Each family includes init,
set, read, exchange, compare/exchange, arithmetic and bitwise operations
with the unspecified, dependency, read, write, acquire, release and full
barrier variants. The 184 entry points include the two address accessors.
These are inline functions compiled into their consumers. The archive
supplies the accompanying thread and event adapters.

Every operation variant currently uses sequentially consistent atomic
access and full fences before and after it. This deliberately exceeds the
weaker variants' minimum ordering requirements. Exclusive initialization
uses C11 `atomic_init` between those fences. Compile-time checks require
native lock-free integer and word operations. The double-word family and
native-only atomic-library setup are described below.

Unsigned backing preserves modular arithmetic without signed overflow.
Compare/exchange returns the observed old value on either success or
failure. Bitwise read-and/read-or return the old value, while add-read,
increment-read and decrement-read return the new value. Storage remains
caller-owned and correctly aligned throughout all accesses. The borrowed
address accessor is for low-level interoperability; ordinary accesses
through it require exclusion from concurrent atomic users.

The signed fixture checks every operation/barrier combination, contended
arithmetic across four participants, and atomic publication before join.
Its publication flag has no CQ readiness source. A test-only observation
loop has a fixed load bound, one monotonic deadline and terminal failure;
production waiting still needs a concrete notification contract. The same
value matrix also runs under host undefined-behavior sanitization.
The [atomic qualification](../../docs/reports/milestones/2026-11-26-erts-atomics.md)
records the complete API inventory, signed results and concrete AArch64
publication fences.

### Double-word atomics

`ethr_dw_atomics.h`, included by the atomic header, implements init, set,
read and compare/exchange in all seven barrier forms, plus the address and
native-capability queries: thirty additional entry points. The selected
upstream header exposes the same definitions. `ethr_dw_sint_t.sint[2]`
preserves the pinned value layout and little-endian low/high word indices.

Double-word compare/exchange returns a boolean. Success preserves the
expected value; failure replaces both expected words with one coherent
observed pair. Value and expected arguments may alias one another, but
must be separate from the atomic backing and remain live throughout the
call. Bit copies preserve signed patterns without signed arithmetic.

The opaque object occupies three words (24 bytes) and requires only word
alignment (8 bytes), matching OTP's minimum base-alignment assumption.
`ethr_dw_atomic_addr` selects a 16-byte-aligned pair within those words.
The object must stay at its original address after initialization through
all users. Do not bitwise copy it to a new address. The returned address
is borrowed; ordinary word accesses require exclusion of atomic users.
Initialization also requires exclusive access.

The implementation uses 128-bit `__atomic` builtins with a `may_alias`
integer type for accesses across adjacent word objects. A compile-time
check requires native lock-free 16-byte operations; the profile supplies
no locked or `libatomic` fallback. Each operation has full fences on both
sides, even for the weaker barrier forms. Exclusive init copies the pair
between fences without reading uninitialized storage. There is no per-object
allocation or cleanup.

The qualified AArch64 compiler emits exclusive-pair loops. Read validates
its loaded pair by writing it back exclusively, so even reads require
writable backing. Native retry loops are lock-free, not wait-free; they do
not park on a CQ or acquire the blocking lock adapters. The native query
reports that qualified capability. The atomic initializer and native
introspection use the same native-only profile described below.

The [double-word qualification](../../docs/reports/milestones/2026-11-27-erts-double-word-atomics.md)
records both alignments, all operation variants, expected-value semantics,
contended CAS, coherent set/read and publication before join. The host
value matrix uses undefined-behavior sanitization. The signed header
profile runs inline and debug consumers and checks their linked native
instruction loops with `check-dw-atomics.py`.

### Atomic initialization and introspection

`ethr_init_atomics` completes the native-only atomic setup. Every supported
width has compile-time lock-free checks, and no fallback lock array or
mutable atomic-library state exists. Initialization returns zero without
allocating, resetting existing atomic values or requiring startup-only
serialization. Calls may repeat concurrently. This implements the atomic
initializer used by upstream common initialization and the bounded native
bootstrap below. Full ERTS CPU/runtime configuration remains open.

The backend advertises native scalar and word-pair types and callable
full-barrier primitive names. Scalar names alias the existing 32-bit and
word-sized operations, with native names such as `add_return_mb` and
`and_retold_mb` mapped to their public equivalents. Public double-word
variants use the native word-pair init/set/read/CAS implementations.
The primitive implementations retain their qualified ordering and layout.

The four `ethr_native_*_ops` entry points follow OTP's null-terminated string
table ABI. The 32-bit and 64-bit inventories each contain thirteen native
`_mb` names, and the double-word inventory contains four. The optional
single-unit scalar double-word inventory is empty because this backend
exposes the word-pair native interface. Implementation-name macros let
upstream's `ethread_info` BIF describe these providers. Blocking lock
fallbacks still do not advertise native spinlocks.

Tables and strings are borrowed immutable image-lifetime storage. The
legacy `char **` result does not transfer ownership or permit writes;
callers must not modify or free either the table or its strings. The
provider does not consult live participant state or allocate snapshots.

`ethr_atomic_info.c` is the archive's eighth member. The
[atomic initialization qualification](../../docs/reports/milestones/2026-11-27-erts-atomic-initialization.md)
records native-name calls, stable tables before and after initialization,
concurrent calls, preserved counters and the existing value/code-generation
regressions through standalone and selected upstream headers.

### Standalone data barriers

`ethr_membar.h`, included by the atomic header, supplies OTP's four ordering
direction bits, `ETHR_MEMBAR`, full, write, read and dependency-read memory
barriers, and the compiler-only barrier. The interface checker compares
the ordering bits with the pinned OTP GCC backend and compiles the macros
through the standalone and selected upstream headers.

Every data-barrier variant currently uses `__sync_synchronize()`, the legacy
builtin whose full-barrier contract OTP permits for ordering at a specific
site. Compiler clobbers surround the builtin, matching OTP's LLVM safeguard.
This exceeds the weaker variants' minimum requirements. Every mask,
including zero, issues a full fence; its argument is evaluated once. Macros
remain void expressions. Existing C11 atomic operations retain their own
previously qualified fences and memory orders.

`ETHR_COMPILER_BARRIER` uses an empty volatile assembly statement with a
memory clobber. It constrains compiler reordering without adding a CPU
barrier. No `ETHR_ORDERED_READ_DEPEND` shortcut or instruction-cache/JIT
barrier is advertised. These data fences do not establish device completion
or resource retirement.

The signed header profile checks all sixteen masks, expression use and
single argument evaluation. Two participants exchange relaxed atomic
payload/flags through four publication-barrier modes before acknowledgement
and exact join. Using atomic payload avoids a C data race even if ordering
fails. The observation loops have a load bound and one monotonic deadline
per admitted publisher; no post-publication event masks the tested path.
This is a bounded test of flags with no CQ readiness source, not a production
wait policy.

`check-barriers.py` checks the linked AArch64 fixture before signing. All
twenty data-fence sites must emit `dmb ish` followed by return; the compiler
barrier must preserve the fixture's store/reload/store without emitting a
CPU barrier. The signed profile repeats these checks for its inline and
debug consumers. The [barrier qualification](../../docs/reports/milestones/2026-11-27-erts-memory-barriers.md)
records the artifacts, code generation and finite publication exercises.

## Terminal diagnostics

`ethr_diagnostics.c` implements OTP's noreturn `ethr_assert_failed` and
`ethr_fatal_error__` entry points. A failure writes its kind, source location,
function and assertion expression or numeric error, then calls native
`abort`. It requests termination of the entire execution domain; it does not
run C finalizers, turn a failing worker into an ordinary exit, release live
objects or claim physical retirement.

Formatting uses a local 240-byte buffer and one console write, without
stdio, heap allocation or `strerror` storage. File, function and expression
fields read at most 48, 40 and 64 bytes respectively and append `...` when
their field limit is reached. Control bytes become `?`; signed line/error
values include `INT_MIN` without signed overflow. A failed console write
still aborts. The runtime console remains serialized and may share buffered
output; this is not an IRQ/signal handler or a reentrant console path, and
one write does not establish atomic physical-console delivery.

OTP supplies `ETHR_ASSERT` and `ETHR_FATAL_ERROR__` through the selected
header. The standalone diagnostics header provides the same macro behavior:
release assertions do not evaluate their arguments; debug assertions
evaluate them once. Fatal error arguments are evaluated once.

Run the terminal controls in their own signed campaign:

```sh
python3 ports/libc/run-tests.py --erts-diagnostics --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/diagnostic-validation
```

The fixture retains startup's stderr lock while a worker owns a native
mutex and fails. The required diagnostic and runtime/kernel abort must
appear without a normal C exit, continuation or C finalizer. Separate
release/debug success cases check assertion argument evaluation. The host
formatter test replaces only the console and abort boundaries, covering
null and bounded nonterminated fields, control bytes, signed extremes and
console rejection; it does not qualify native abort or stdio locking.

The [qualification record](../../docs/plans/evidence/erts-terminal-diagnostics/summary.json)
passed 21 signed cases and three deployment controls on two guest CPUs.
Both terminal controls emitted their context from the worker LP and reached
runtime and kernel abort without a normal exit or finalizer. The later
survivor passed. The interface check matched 292 public functions and 33
native callable names, retained all nine archive objects, compiled normal,
inline and debug modes, and rejected all four mismatch/profile controls.
Host release and debug formatter matrices passed AddressSanitizer and
UndefinedBehaviorSanitizer. The
[serial excerpts](../../docs/plans/evidence/erts-terminal-diagnostics/serial-excerpts.txt)
preserve the terminal observations; these do not establish physical
retirement. Full thread startup and configure integration remain open.

## Native event adapter

`ethr_event.h` and `ethr_event.c` implement the event portion of the
pinned ERTS thread-library interface over the C native-worker event API.
The staged Knytos header selects this definition. The full ERTS build still
needs to select and link that backend through its build machinery.

Positive nanosecond timeouts round up to milliseconds. Zero checks
readiness immediately, and every negative timeout selects an indefinite
wait. An optional nonnegative spin count goes directly to the registered
native wait. Set and reset preserve the interface's memory barriers.
Successful waits return zero and timeout returns `ETIMEDOUT`.

Creation reports `ENOMEM` on allocation failure. Destruction returns
`EBUSY` for an admitted waiter and requires external lifetime
synchronization against new producer and waiter calls. Unexpected native
operation failures, including wait rejection and shutdown, abort the
domain. The enclosing backend must arrange a terminal set and join for
indefinite waiters during cooperative shutdown.

The standalone signed fixture compiles the adapter into its translation
unit and calls the ERTS event API. Run it through the ordinary worker
qualification with a new output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/event-adapter-validation
```

## Thread-specific data adapter

`ethr_tsd.h` and `ethr_tsd.c` provide the pinned thread library's four
public keyed TSD functions. Keys use the native runtime's permanently
retired identities. Each participant's value remains borrowed through
its worker lifetime. Invalid or stale keys return `EINVAL` from set/delete
and null from get. Creation returns `EAGAIN` at the sixteen-key limit
and `EOVERFLOW` if the identity counter is exhausted.

The public key creation entry point supplies no destructor. The enclosing
backend can register its internal thread-event cleanup with
`knytos_tsd_key_create_with_destructor`. That callback runs with a cleared
key and a live participant identity before native exit. The standalone
TSD fixture combines both adapters and retains its shared argument through
join while the destructor releases the event and its containing allocation.

The [thread-specific event owner](#thread-specific-events) binds a reserved
native destructor key to its owned event list. The staged Knytos header
selects these definitions; ERTS CPU/runtime configuration remains open.

## Thread-specific events

`ethr_ts_event.h` and `ethr_ts_event.c` provide the pinned public object
fields, flags and nine lifecycle entry points. The integrated native
bootstrap reserves this key during `ethr_init` and constructs the startup
event during `ethr_late_init`. For independent adapter use, startup calls
`knytos_ethr_ts_events_init` before workers use this interface. It reserves
one native destructor key for the domain lifetime, returning `EAGAIN` for
key admission rejection and `EBUSY` on repeated initialization. Workers
cannot initialize that shared key. The option initializer described below
supplies event allocator hooks and logical main-thread bounds. Actual
ERTS runtime configuration remains open; native locks do not use reader groups.

Each adapter participant lazily owns one context containing a cached event
and its temporary duplicates. The bounded profile permits sixteen live
events per participant. Capacity rejection returns `EAGAIN`, allocation
failure returns `ENOMEM`, and construction preserves the output and
existing context on rejection. All fallible preparation precedes object
publication. Participants retain their own exact adapter generation.
Foreign native threads remain outside this profile.

Get and use mark an object busy. A busy duplicate receives a fresh native
event and copies only borrowed queue metadata, atomic flags and scalar
state. Its set/reset state is independent. Upstream peek flags are copied
too; a temporary is released only after both busy and peek claims leave.
The private ownership links remain separate from public queue links.
Borrowed `udata` must clear before disposal. Cross-thread producers borrow
the embedded event and finish their calls before the owning participant
destroys it. Readiness alone establishes no producer quiescence.

Free rejects busy, peeked or nonempty-`udata` objects with `EBUSY` and
foreign pointers with `EINVAL`. `knytos_ethr_ts_events_clear` clears the
calling participant. It may release an idle prefix before a busy rejection;
the same context retains every remaining owner for a later clear. Native
event destruction also preserves a rejected object. Usage counts change
after actual object release and supply telemetry, not lifetime authority.
Exclude concurrent producers and new uses during destruction.

Worker TSD cleanup disposes idle contexts outside runtime guards. An exit
with live claims aborts while retaining the owner graph. A later user
destructor may create a fresh context, which is cleaned in the next native
destructor pass. The native four-pass termination bound remains in force.
Startup explicitly clears its own context after last use. Exact worker
joins still fence argument and slot reuse.

Run this ownership qualification in a fresh guest through its own profile.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-tse --smp 2 --output target/erts/thread-events-validation
```

The [thread-event qualification](../../docs/reports/milestones/2026-11-26-erts-thread-events.md)
records nested lifetimes, partial cleanup, capacity and key rejection,
cross-CPU delivery, destructor rearming and the fatal live-borrow control.

### Event late configuration

`knytos_ethr_ts_events_init_opt` accepts the pinned `ethr_late_init_data`
layout. It copies and validates options before publishing the reserved
key. Negative counts return `EINVAL`, and more than four logical main
thread slots return `ENOTSUP` in this bounded profile. The ordinary
initializer supplies default options. Reconfiguration returns `EBUSY`.
Reader groups remain advisory for the current native read/write backend.

Contexts and list nodes use the selected `mem.std` callbacks. An incomplete
standard allocator tuple selects the complete malloc/realloc/free fallback,
as upstream does; incomplete short-/long-lived groups inherit that tuple.
The current event adapter uses only the standard group and fixed-size
allocations. Native event storage continues through the C runtime's own
allocator. Each context captures its allocator for matching disposal,
including worker TSD cleanup and explicit clear.

Callbacks supply aligned storage and consuming local deallocation, return
normally and retain their code and backing for the domain lifetime. They
run outside runtime guards. Per-participant callback guards reject
reentrant event ownership operations and status mutation with `EBUSY`;
pointer/void interfaces treat such failures as terminal. Read-only main
status and usage snapshots remain available during callbacks. Allocation
failure preserves existing owners and releases the complete unsubmitted
prefix through its captured allocator.

`ethr_get_main_thr_status` reads the cached event's flag without allocating.
No cached event means off. `ethr_set_main_thr_status` requires a cached
event and an enabled index within the captured bound; disabling clears
the flag and index. Both operate on the current participant. Temporary
duplicates copy their existing status snapshot. Clear and fresh contexts
start off. These are ERTS metadata and do not alter native admission,
placement or kernel platform classification.

The [late event configuration qualification](../../docs/reports/milestones/2026-11-27-erts-event-configuration.md)
records callback capture, preparation failure, reentry rejection, whole
fallback selection and main-status lifetime.

## Native early and late initialization

`ethr_init` and `ethr_late_init` implement the pinned public lifecycle for
the bounded native backend. Call both from startup before creating workers.
Early initialization copies creation hooks and admits the identity and
thread-event TSD registrations. One pending owner retains the unpublished
identity key; event-key rejection cancels it before returning. No hooks or
phase change is committed on rejection, so that unstarted initialization
may be retried. Successful keys remain reserved for the domain lifetime.
The atomic and mutex-library setup is stateless. Native parking ignores
the CPU-count optimization hint; this does not discover CPUs or fabricate
platform configuration.

Late initialization validates and copies allocator/main-thread options and
prepares the cached startup event with the selected allocator. Its complete
pending event owner handles allocation/event rejection. Only success
publishes immutable configuration and permits worker creation. Allocation
failure leaves the early phase and reserved keys intact, with no cached
event or published allocator configuration; a later call may retry that
unstarted preparation. Main-thread status remains explicit local metadata.

Startup receives `EACCES` for late initialization before early setup or
worker creation between the two phases, `EINVAL` for repeated successful
early/late initialization, and `ECANCELED` after the shutdown fence. Worker
initialization calls return `EACCES`. During late allocator callbacks,
reentrant initialization, thread control and handler registration return
`EBUSY`; read-only event status remains available. Private independent
bootstrap helpers cannot replace an already reserved event key or hook set.

Each admitted worker establishes its exact adapter identity and constructs
its cached event before invoking the child hook and completing the existing
parent handshake. Exit handlers can still borrow that event before native
TSD teardown. If event construction fails after native start was accepted,
the domain aborts with the worker record and prepared callback borrow
retained; it does not return a partial creation or call parent cleanup while
child initialization is uncertain. This is a terminal policy for the
current profile, not a recoverable per-thread admission error.

Normal return, explicit worker exit and detached drain use the existing
handler/TSD cleanup and exact joins. Startup explicitly clears its own event
after excluding producers and draining workers. Logical event release and
join do not establish physical root retirement. The profile still supports
three workers with default stacks; full ERTS thread capacity, actual stack
bounds, CPU/runtime data, thread progress and build integration remain open.

```sh
python3 ports/libc/run-tests.py --erts-bootstrap --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/bootstrap-validation
```

The [native bootstrap qualification](../../docs/reports/milestones/2026-11-27-erts-native-bootstrap.md)
passed 21 signed bootstrap cases and the separate 28-case header regression,
with three deployment controls in each guest. Both use the identical
nine-member archive. The interface check matched 294 public functions and
33 native callable names, retained 327 references, compiled normal, inline
and debug modes and rejected all four mismatch/profile controls. Key
admission rollback, both allocation prefixes, copied options/hooks, automatic
events, normal/explicit exit and detached drain passed. The accepted worker
allocation-failure control aborted its domain without child/parent cleanup
or ordinary return. The existing header fixtures and four barrier/double-word
instruction checks also passed. The
[evidence manifest](../../docs/plans/evidence/erts-native-bootstrap/summary.json)
and [serial excerpts](../../docs/plans/evidence/erts-native-bootstrap/serial-excerpts.txt)
retain identities and observations, including the still-open store limitation.

## Bounded thread adapter

`ethr_thread.h` and `ethr_thread.c` provide startup-only create, join and
detach, self identity and identity comparison.
Three static start records retain each callback, borrowed argument and
returned pointer until exact native join succeeds. The adapter assigns
generation-qualified identities and rejects stale joins after slot reuse.
Join rejection retains the same record and native owner for observation.

The qualified options are joinable or detached threads with the default
stack and a name of up to fifteen bytes. Explicit stack suggestions return
`ENOTSUP`. A full roster returns `EAGAIN`. Callers retain argument data
and callback code through successful join. Startup uses identity zero,
and adapter-created workers retain their identities throughout TSD
destructor callbacks.
The adapter reserves one native TSD key for its domain lifetime.

This adapter uses native worker slots 1 through 3 on LP 1 and requires a
guest with at least two CPUs. Signed thread and stack admission remains
authoritative. Expanding the roster, background detached-thread reaping,
stack limits and thread-progress integration remain
backend work. The enclosing backend must also arrange terminal wakes and
join every worker during cooperative shutdown.

### Creation callbacks

Startup may call `knytos_ethr_threads_init` once, before the first create
attempt that reaches native submission. It copies the upstream
`ethr_init_data` callback layout. Null selects empty hooks, and omitting
this initializer also leaves the hooks empty. Registration returns
`EBUSY` after registration or creation has started, `EACCES` from a worker
and `ECANCELED` after the shutdown fence. This initializer covers the
standalone adapter; the integrated native lifecycle above uses `ethr_init`
and `ethr_late_init` instead.

Prepare runs on startup after local validation and record reservation.
Its returned pointer stays in that record through native submission.
The child establishes its adapter identity, invokes its captured child
callback and publishes completion before waking startup's private CQ.
Startup rechecks that completion after unrelated wakes and invokes parent
cleanup only after the child callback returns. A rejected native start
invokes parent cleanup without running the child callback. Preflight
rejection invokes neither callback. Parent cleanup may overlap user entry;
the child must stop using the prepared pointer before returning its hook.
Callbacks retain responsibility for their pointees and code lifetimes.

The record and exact worker owner remain retained through this handshake.
An unexpected handshake-wait error aborts the domain without returning an
unowned partial creation. Child initialization must return or abort; an
adapter thread-exit request from that callback aborts. Reentrant create,
join, detach, drain and callback registration from startup callbacks return
`EBUSY`. Callbacks run outside runtime guards. Shutdown still needs child
initialization progress before it can drain workers.
The [signed initialization qualification](../../docs/reports/milestones/2026-11-26-erts-thread-init.md)
records callback ordering, rejected-start cleanup, early wakes and the
fatal child-exit control, with the earlier thread suite repeated separately.

### Stack-limit metadata

`ethr_get_stacklimit` and `ethr_set_stacklimit` implement the pinned public
interface over participant-local borrowed metadata. Startup begins with a
null value, and each new adapter worker generation also begins with null.
Installing null returns `EINVAL`. After a non-null value is installed,
every replacement, including null, returns `EACCES`, preserving upstream
error precedence. A native worker outside the adapter has no stack-limit
slot; get returns null and set returns `EINVAL`.

Workers can install their value from the child-initialization hook. Their
value remains available through user entry, exit handlers and TSD cleanup.
Exact join or shutdown drain clears the retired record before slot reuse.
Startup retains its own value for the domain lifetime. These operations
allocate no storage and consume no additional TSD key. The caller retains
the represented stack boundary's lifetime; stack backing stays owned by
the native runtime and kernel.

Automatic discovery of native stack bounds remains open. The backend must
capture actual stack boundaries before running ERTS code that requires
non-null limits. A sampled stack pointer or sampled high-water mark does
not establish a reserved stack boundary. The signed metadata fixture uses
address markers and qualifies no native stack geometry or overflow guard.
The [signed stack-limit qualification](../../docs/reports/milestones/2026-11-26-erts-stacklimit.md)
records participant isolation, error precedence, cleanup lifetime and
fresh metadata after slot reuse.

### Thread names

Creation copies its optional name before native start and rejects names
longer than `ETHR_THR_NAME_MAX` (15 bytes) with `EINVAL`.
`ethr_setname` changes the calling thread's adapter name and ignores an
overlong input, matching the pinned interface. Null selects an empty name.
`ethr_getname` copies a consistent, null-terminated name for the supplied
generation-qualified identity. A small buffer returns `ERANGE`, an invalid
or retired identity returns `EINVAL`, and overlap with a rename returns
`EAGAIN`. Errors preserve the output buffer.

Name publication uses fixed atomic storage and one sequence check per
reader attempt. The calling thread is the sole writer while it is live.
Startup initializes the name before publication and clears its generation
after confirmed join or rejected native start. A retained thread's name
remains available during its TSD destructors. Startup has its own name
under identity zero.

### Detached custody and shutdown

Detached creation and `ethr_thr_detach` transfer result-observation interest
to the adapter. Public join rejects detached identities with `EINVAL`.
The native owner and its record remain retained until an explicit drain,
including after callback return and TSD cleanup notification.

Startup calls `knytos_ethr_threads_shutdown` after publishing terminal
predicates and wakes for every worker. The drain permanently fences new
creation, joins every retained worker and releases records after confirmed
exit. A rejected join retains that record and every remaining record for
a later drain call. Previous successful joins stay released. Repeating a
completed drain succeeds. Creation after the fence returns `ECANCELED`.

Retain callbacks, arguments and shared state through successful drain.
The drain settles both joinable and detached workers and discards their
return pointers. Callers remain responsible for any pointees they own.

### Explicit thread exit

`ethr_thr_exit(Result)` stores the worker's return pointer and enters the
native terminal exit path. TSD destructors still run with the participant's
identity and name available. A joinable worker returns its stored pointer
only after confirmed native join. Detached workers retain their record
until the shutdown drain confirms exit.

This entry point supports adapter-created workers. It aborts when called
from startup or recursively from TSD cleanup. The caller must leave no
Rust owner or guard requiring stack unwinding above the terminal C boundary.
Registered adapter exit handlers run before native TSD cleanup on both
normal return and explicit exit. Full ERTS still needs to install its own
callbacks and connect thread-progress cleanup before this boundary.

### Exit handlers

`ethr_install_exit_handler` admits up to sixteen callbacks registered by
startup. A null callback returns `EINVAL`, a full registry returns
`EAGAIN`, worker registration returns `EACCES`, and registration after
the shutdown fence returns `ECANCELED`.

Each adapter worker exit captures the published handler count and invokes
that snapshot in reverse registration order before TSD destructors. Entries
remain immutable after publication and callbacks run outside runtime
guards. Registration by startup while workers are live can affect a
later exit snapshot. Callback code must remain callable through every
retained worker's exit and drain.

An exit request from a running handler aborts the domain. A later recursive
request from TSD cleanup reaches the native recursive-exit guard. Handler
registration remains fixed storage for the domain lifetime.

## Mutex adapter

### Library initialization

`ethr_mutex_lib_init` and `ethr_mutex_lib_late_init` supply the two
mutex-library startup entry points used by OTP's common thread initializer.
They return zero without allocation, shared mutable state or changes to
existing locks. CPU counts, reader groups and main-thread counts select
spin defaults and reader arrays in upstream implementations. Native Knytos
locks park directly, maintain their own writer admission and use no such
arrays. All integer hints, including negative and extreme values, are
therefore ignored without arithmetic. These counts do not grant thread
admission, placement or main-thread status.

Calls are repeatable from registered participants before lock construction,
while owners and waiters remain live, and after disposal. They do not
initialize thread events, creation callbacks, CPU discovery or the complete
`ethr_init`/`ethr_late_init` lifecycle. Per-object option validation remains
part of each object's initializer.

The [signed qualification](../../docs/plans/evidence/erts-mutex-initialization/summary.json)
passed all 28 header-profile cases and three deployment controls on two
guest CPUs. The interface check matched 290 public functions and 33 native
callable names in normal, inline and debug modes; all four rejection
controls rejected. The lock fixture repeats early and late initialization
from startup and workers with extreme integer hints, checks unchanged
allocation counts and errno, and retains held mutex/read/write owners and
a registered condition waiter across those calls. Signaling, exact joins
and final zero-block cleanup succeed. The
[serial capture](../../docs/plans/evidence/erts-mutex-initialization/serial-excerpts.txt)
and [linked AArch64 instructions](../../docs/plans/evidence/erts-mutex-initialization/aarch64-initializers.txt)
retain the observations. The instruction capture shows both initializers
returning zero directly, with no hidden allocation or mutable setup.

### Mutex operations

`ethr_mutex.h` and `ethr_mutex.c` implement the pinned ERTS mutex
interface over native nonrecursive mutexes. Default and optional
initialization return `ENOMEM` on allocation failure and reject spin
counts below -1 with `EINVAL`. Optional spinning goes directly to the
registered native wait. The backend supports the POSIX-compliant locking
convention.

`ethr_mutex_lock` uses event-only acquisition with no artificial deadline.
The owner must eventually unlock, and the enclosing backend must arrange
that progress during shutdown. Unexpected lock or unlock errors abort
the domain. Trylock returns `EBUSY` while held, including a same-thread
attempt, and `EINVAL` for an absent native object.

Destruction requires external lifetime synchronization. It returns
`EBUSY` while ownership or waiter metadata remains live, retaining the
object on rejection. Successful destruction clears the wrapper's pointer.
Callers keep the object live throughout all waits. Native wait rejection
removes only waiter interest before the adapter takes its fatal path.
The staged header selects these types and entry points. The full ERTS
backend still needs build integration and thread-progress/shutdown
qualification.

Run the mutex, condition and read/write checks in their own signed campaign.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-mutex --smp 2 --output target/erts/mutex-validation
```

The mutex and worker-libc profiles use separate guests. A combined
34-case campaign encountered object-store extent exhaustion while the
catalog persisted another deployment, despite free disk space. The
[qualification record](../../docs/reports/milestones/2026-11-26-erts-mutex-adapter.md)
preserves that failure and the separate repeats.

## Condition-variable adapter

`ethr_mutex.h` and `ethr_mutex.c` also provide the six `ethr_cond_*`
entry points and the upstream option layout. Initialization returns
`ENOMEM` on allocation failure and rejects spin counts below -1 with
`EINVAL`. Optional spinning goes directly to the native registered wait.
The adapter supports the POSIX-compliant signaling convention, including
signal and broadcast without holding the associated mutex. Use matching
POSIX-compliant mutex and condition options when the caller needs that
convention.

Wait requires the caller to own the mutex nonrecursively. The native
operation registers condition interest before releasing the mutex, then
waits for selection and mutex reacquisition without deadlines. Successful
return always owns the mutex. Signal selects one pending waiter and
broadcast selects all. Neither retains a token in the absence of waiters.
Callers must publish and recheck predicates under their mutex. Wake order
and lock reacquisition have no FIFO guarantee.

Unexpected wait, signal or broadcast errors abort the domain. Busy
destruction returns `EBUSY` and retains the condition. Successful
destruction clears its wrapper pointer. External lifetime synchronization
keeps both objects live throughout selection and reacquisition and excludes
concurrent entry during destruction. The enclosing backend must arrange
terminal predicates, selection, unlock and exact joins before disposal.
Full ERTS linkage and coordinated shutdown remain open.
The [signed qualification](../../docs/reports/milestones/2026-11-26-erts-condition-adapter.md)
records the condition fixtures and the separate timed-worker regression.

## Read/write mutex adapter

`ethr_rwmutex.h` and `ethr_rwmutex.c` implement the eleven upstream
read/write mutex entry points. All supported type and lifetime options
use the same native algorithm. Reader groups are unused hints, matching
OTP's pthread backend. Spin counts go directly to registered waiting.
Invalid option enums or spin counts below -1 return `EINVAL`, and
allocation failure returns `ENOMEM`.

Readers can share and nest acquisition. A queued writer blocks new
readers, while an existing reader can nest. Each read acquisition requires
one matching release. Exclusive acquisition cannot nest or upgrade a read
lock. Try-lock reports held or incompatible ownership as `EBUSY`, and an
absent native object as `EINVAL`. Both blocking acquisitions wait for
events without deadlines. Unlock notifications follow guard release;
waiters recheck lock state rather than treating a wake as acquisition.
Selection and reacquisition have no FIFO guarantee.

`ethr_rwmutex_size` reports the native heap storage beyond the embedded
wrapper, excluding allocator overhead. Busy destruction preserves the
object, and successful destruction clears its pointer. External lifetime
synchronization keeps the object live through waits and excludes concurrent
entry during destruction. Unexpected blocking or unlock errors abort the
domain after native waiter-interest withdrawal. Callers pair read/write
acquisitions with their corresponding release and supply unlock and terminal
shutdown progress before entering an indefinite wait.

Full ERTS type selection, linkage, thread progress and shutdown remain open.
The [signed read/write qualification](../../docs/reports/milestones/2026-11-26-erts-rwmutex-adapter.md)
records shared readers, both blocked access modes, heap size and fatal
foreign release, with the timed worker suite repeated separately.

## Low-level lock fallbacks

`ethr_lock.h` and `ethr_lock.c` provide the four `ethr_spin*` entry points
over `ethr_mutex`, and the six low-level `ethr_rwlock`/read/write entry
points over `ethr_rwmutex`. This follows OTP's blocking fallback model.
The types use registered event-only acquisition with the same owner,
busy-destruction and error contracts as those underlying adapters.
The backend must permit parking and provide unlock and terminal shutdown
progress at each call site. These fallbacks advertise neither native
spinlocks nor native read/write spinlocks. `ethr_compiler_barrier` uses
a C11 sequentially consistent compiler fence.

The signed archive fixture includes only the public aggregate header,
links the separate objects, and retains its shared input through exact
joins. The `--erts-mutex` profile qualifies blocking spin contention,
shared readers, blocked readers and writers, busy destruction and fatal
foreign release through these entry points.
The [archive qualification](../../docs/reports/milestones/2026-11-26-erts-lock-archive.md)
records the signed fixtures, archive members and separate-object link map.
