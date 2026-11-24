# Erlang M0 — architecture discovery

Date: 2026-11-24. Branch: `feature/erlang-runtime`.
Knytos source baseline: `19cff1ad`; Sitas input:
`0b2a85c144f178dd85992e42889464937a498f74` from `dependencies.lock`.

## Decision and qualification

Recommend M1 as a **single-threaded, TLS-free C application environment** over
the existing Rust runtime. A statically linked AArch64 C program does not
intrinsically require a kernel change: the compiler/linker probe produces an
`ET_EXEC` with the expected machine, entry and page-separated permissions.
Native signed C execution remains an M1 acceptance test, not an M0 result.

Recommend AtomVM **0.6.6**, without SMP or the task driver, for the first port.
Use its embedded platform boundary, not the Unix platform as a compatibility
layer. Pin an OTP 27 compiler separately. The first native AtomVM binary,
package and allocation/stack measurements remain M2 gates.

Do **not** treat ERTS as fitting the existing C profile. Native TLS,
thread/event semantics, allocator carriers, image/package layout and resource
budgets need separate decisions before M5. This is a feasibility finding,
not a proposal to increase limits during discovery.

No runtime, syscall, kernel, dependency-lock or service implementation was
changed. The two C files in the evidence directory are compile-only probes.
Neither AtomVM nor ERTS has been executed on Knytos. The
[C runtime design](../architecture/c-runtime.md) is proposed implementation
guidance, not an existing public ABI.

## Reproduced baseline

Executed against the source baseline with the pinned Rust nightly
`nightly-2026-11-14`:

| Check | Result |
| --- | --- |
| `cargo test` for `knytos-rt`, `knytos-app`, `knytos-app-sitas`, all features, library tests | 85 passed: 66 runtime, 14 application, 5 async-adapter tests |
| `python3 scripts/tests/test-ci-runtime-inputs.py` | 6 passed |
| `scripts/run-aarch64.sh release --minimal --build-only --instance erlang-m0` | Signed service bundle, kernel and images built successfully |
| `scripts/run-aarch64.sh release --minimal --smp 4 --timeout 180 --instance erlang-m0` | Passed minimal validation over the full window on QEMU 11.1.3; boot completion and DHCP observed |
| Freestanding C compile/link probe | Passed; not signed or executed |
| AtomVM host `MinSizeRel` build with SMP/task driver disabled | Passed; version/help commands executed; no Erlang workload |

The boot kernel SHA-256 is
`519062f699e54bff0423a8db73aa0647e3520f493786a0b3b50916b83aca9de7`.
The [serial capture](evidence/erlang-m0/baseline-serial.txt),
[host-test results](evidence/erlang-m0/host-tests.txt) and
[evidence summary](evidence/erlang-m0/summary.json) retain results, input hashes,
tool versions and the boot manifest. The minimal run did not execute the native
regression suite; it establishes the operational baseline only.

Two host preparation failures are kept separate from passing executions:
the sandbox initially blocked Git DNS, and an attempt to reuse the built
kernel was rejected because the runner provisions fresh management inputs.
That invocation never launched a guest. The normal runner passed. Neither
failure is an Erlang or guest-runtime failure. The earlier unexplained
[CI boot timeout](../reports/investigations/2026-11-24-aarch64-minimal-timeout.md)
remains unresolved; this baseline does not close it.

Reproduce the host tests from outside the freestanding Cargo configuration:

```sh
cd /tmp
cargo +nightly-2026-11-14 test \
  --manifest-path /path/to/knytos/Cargo.toml \
  -p knytos-rt -p knytos-app -p knytos-app-sitas --all-features --lib
```

## Upstream inputs and compiler compatibility

These are discovery pins, not additions to the OS build dependency closure.
No upstream repository was vendored or modified.

| Component | Selected reference | Commit | Disposition |
| --- | --- | --- | --- |
| AtomVM | `v0.6.6` | `ff993a80963298b532c1e573f883951ecaac9fef` | Initial stable interpreter baseline |
| ERTS/OTP | `OTP-29.1.2` | `d199a49a01a2079afb6f2c5c9ae047cd4555e68d` | Contemporary source investigated for later port |
| Historical OSE | `OTP-18.3` | `5e1b5ef47f3f2b898d30e0425823835bd9a574d4` | Porting reference only |
| Picolibc | `1.8.12` | `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff` | Recommended libc candidate; not cross-built yet |
| AtomVM host compiler | `OTP-27.3.4.19` | Release tag selected; source/toolchain not yet adopted | Mandatory host build qualification before M2 BEAM packages |

AtomVM's [0.6.6 release notes](https://github.com/atomvm/AtomVM/releases/tag/v0.6.6)
recommend OTP 27 and explicitly exclude OTP 28 support. The installed host
compiler reports OTP 29. Do not use it to qualify the selected AtomVM release.
Select [OTP 27.3.4.19](https://github.com/erlang/otp/releases/tag/OTP-27.3.4.19)
for the AtomVM host compiler build; compatibility still needs execution tests.
The eventual OTP 29.1.2 cross-build needs its own matching host bootstrap;
the installed ERTS 17.0.3 measurement is not that source build. Pin both host
compiler closures and artifact hashes when adopting them. No toolchain was
installed or replaced in M0.

The AtomVM host build omitted MbedTLS because it was not found, and emitted
unused-parameter, generated non-prototype, Unix array-extension and duplicate
library warnings. It was not a warning-clean target build; none of those host
features or diagnostics is a qualified Knytos libc/platform result.

The inspected AtomVM tree has Apache-2.0 / LGPL-2.1-or-later SPDX alternatives
in the platform/core files and a root Apache license. OTP includes Apache-2.0
licensing. Picolibc's `COPYING` enumerates per-file licenses, including tooling
with different terms from library code. M1 must retain notices and inventory
the exact linked files and build tools; do not infer one license for an entire
source tree from its project description. Newlib was assessed from its
upstream manual, not adopted as a source dependency. This was not an advisory
scan or a release-provenance review of any candidate.

## Knytos inventory and dependency matrix

Paths below refer to the current repository. Existing Rust APIs are evidence
of mechanisms, not a claim that a C binding already exists. Host tests qualify
the public owner logic; kernel readiness and thread behavior also require the
native suites identified below.

| Facility / upstream requirement | Existing source and API | Status, gap and proposed milestone |
| --- | --- | --- |
| ELF startup, C ABI | `crates/catten/src/service/loader.rs`, `validate_user_elf`; `crates/knytos-services/link.x`; `knytos_rt::entry!` | ELF64 little-endian AArch64 `ET_EXEC`, fixed addresses, page-granular RX/R/RW loads. No dynamic linker or relocation service. M1 adds a C-facing startup wrapper; keep Rust launch context. |
| Constructors / C exit | `knytos_rt::run_main`, `knytos_app::run`, `AppExit`, lifecycle in `knytos-rt/src/lib.rs` | No general C CRT, init/fini-array runner, `argc` builder or process-wide C exit contract. M1 must run constructors once and distinguish main return, worker exit and domain abort. |
| ELF TLS / thread pointer | AArch64 `lp/ops.rs`, `lp/thread_context/mod.rs`; loader only maps `PT_LOAD` | No TLS image/TCB initialization or per-thread user TP save/restore identified. LP-base helper functions also address `TPIDR_EL0`. M1 disables TLS; M3/M5 need a general architecture contract. |
| Allocation | `knytos_rt::HeapLock`, `heap`, `Context::heap_layout`; loader adaptive heap | A mapped runtime arena exists; no C `malloc` facade. M1 shares its allocator, with C allocation layout metadata and checked arithmetic. No independent uncharged `sbrk`. |
| Memory mappings | `knytos_rt::owned::{OwnedMemory,MappedMemory}` | Owned allocation/map/unmap supported. Not POSIX address-space reservation, `mmap` remapping, or `mprotect`. Later ERTS carriers must retain actual owners; mapping/protection assumptions need review. |
| Threads / join | `owned/completion.rs`, `ThreadHandle::spawn`, `join_preserving`, `Workers` | Generation-bound spawn/join exists; spawn takes an entry and LP, not a C argument/stack/TLS pointer. M3 needs an admitted trampoline/start-record owner and synchronization. Do not treat detaching a handle as termination. |
| Mutex / condvar / TSD | `knytos-rt`, syscall catalog; Sitas shard backend | Rust locks and wake/CQ mechanisms exist; no general public futex/address-wait/condvar or pthread ABI was identified. Build only required semantics in M3; no polling substitutes. |
| Monotonic time | `knytos_syscall::monotonic_clock`, `knytos_rt::deadline::Deadline` | Returns counter/frequency. M1 converts with checked wide arithmetic; monotonic deadlines must survive unrelated wakes. |
| UTC | `crates/knytos-protocol-time/src/lib.rs`, `TimeSnapshot`, `OP_NOW`; `time` service | Authorized IPC, not a realtime syscall. Unavailable/unsynchronized is explicit. M1 C API returns unavailable without authority; M2 must decide startup/cached time policy before adapting void `sys_time`. Never return uptime as UTC. |
| Timers | `owned::Completion::timer`, `cq::wait` | Owned timer completion and checked timed CQ wait exist. No per-Erlang-timer kernel object needed: one VM timer heap plus earliest-deadline wait suffices initially. |
| Polling / notifications | `knytos-rt/src/cq.rs`, `crates/catten/src/completion/delivery/readiness.rs` | ABI 4 syscall 88 separates readiness/deadline/admission errors. Default CQ plus four loader-provisioned shard CQs. One consumer per ring; lifecycle wakes CQ 0. M2 uses one reactor; M3 needs an explicit SMP waiter design. |
| Console / diagnostics | `knytos_rt::log`, syscall `log_str` | Length-delimited logging available. M1 binds bounded output and Picolibc console callbacks. No terminal or global file descriptors implied. |
| Entropy | `knytos_rt::entropy::{fill,word}` | Fallible kernel entropy; no deterministic fallback. Adapt only when required by the selected VM profile. |
| IPC and native resources | `owned/ipc.rs`, `Connection`, `PendingCall`, `IncomingMessage`, `ReplyToken` | Ownership-preserving submission, move/borrow and authenticated receive exist. M4 C/VM objects must retain these owners, including on timeout or finalizer notification. |
| Networking | `knytos_app::socket`, `OwnedSocket`, protocol and `tcpip` service | TCP/UDP clients exist; no BSD sockets ABI. Socket borrows, remote close and async progress require adaptation. Exclude M2; conventional distribution in M7 is separate work. |
| Storage / files | `knytos_services::objstore`, `knytos_services::fs` and their protocol crates | Capability/protocol access, not ambient POSIX paths or an ERTS filesystem backend. Initial AVM package stays in authenticated ELF rodata. |
| Naming / bootstrap grants | `knytos_app::Application::{from_context,acquire,publish}` | Use the grant controller and signed descriptor, not unrestricted raw name lookup. M4 binding requires an authorized service and retained connection. |
| Launch / lifecycle | `Context::lifecycle`, `ShutdownRequest`, `owned::DeployedArtifact` | Scoped deployment and signed retirement principal exist. Join workers and drain operation owners before acknowledgement. A thread exit alone does not shut down a runtime domain. |
| Authentication / placement | `knytos-launch/{signature_note,execution_target,deployment,release}.rs`, `tools/cluster-sign` | CLS2 ELF and CDEPLOY6 variants, exact runtime ABI 4. Embedded modules inherit full-ELF coverage; later external archives need signed binding. No competing Erlang deployment format. |
| Checkpoint / relocation | Existing deployment/catalog and lifecycle code; plan M8 | No generic Erlang snapshot/restore API. App schema, durable transition and effective external-resource fencing remain M8 work. Never activate before source fencing. |

Relevant existing checks include `scripts/tests/test-app-sdk.py`,
`test-ci-runtime-inputs.py`, `test-boot-payloads.py`, the runtime/application
library tests, `crates/catten/src/self_test/sitas.rs`, scheduler lifecycle and
thread-admission fixtures. Only the checks in the baseline table were executed
for this discovery; listing a test does not claim it passed here.

## AtomVM platform findings

Inspected source at the pinned commit:

- [`src/libAtomVM/sys.h`](https://github.com/atomvm/AtomVM/blob/ff993a80963298b532c1e573f883951ecaac9fef/src/libAtomVM/sys.h)
  supplies `sys_poll_events(GlobalContext *, int timeout_ms)`, with `0` meaning
  nonblocking and `-1` infinite; platform init/free, listener/select ownership,
  clocks, package/module loading, port creation and system-info hooks also
  belong here. `sys_signal` is conditional on SMP/task-driver support. This
  is an actual source boundary, not the plan's illustrative singular name.
- [`src/libAtomVM/CMakeLists.txt`](https://github.com/atomvm/AtomVM/blob/ff993a80963298b532c1e573f883951ecaac9fef/src/libAtomVM/CMakeLists.txt)
  requires C11, generated gperf BIF/NIF tables and libm. It probes libc/POSIX
  functions and optional zlib. The adapter must cross-compile honest feature
  checks; Unix host detection results cannot become Knytos feature claims.
  `AVM_DISABLE_SMP=ON` and `AVM_DISABLE_TASK_DRIVER=ON` produce the desired
  core definitions. Version 0.6.6 has no JIT/precompiled-native build options
  found in this inspection. The newer development documentation is not its
  build contract.
- The top-level CMake dispatcher recognizes Unix hosts. A dedicated
  `src/platforms/knytos` entry/build harness is needed. The generic Unix
  platform unconditionally brings socket sources and dynamic port loading;
  reusing it wholesale would import unwanted semantics.
- STM32 demonstrates direct package access and minimal platform hooks, but
  its no-op event polling and uptime-backed wall clock are **not** acceptable
  implementations for Knytos. ESP32 demonstrates queue-based platform/SMP
  integration but depends on FreeRTOS and platform SDKs. Reuse interfaces and
  algorithms, not hardware dependencies or incomplete stub behavior.
- `smp.h` requires mutex, condvar, RW lock and scheduler-thread operations in
  SMP builds. The Unix implementation also uses native thread-local state.
  A single scheduler avoids this dependency for M2; it does not prove SMP
  readiness. An event already available must be rechecked after registration.
- NIFs/resources in `erl_nif.h` and `resources.c` support managed resources,
  selectable events and monitor callbacks. M4 still needs Rust-owner custody,
  bounded message delivery and a nonblocking finalizer policy. VM references
  are not duplicate capability owners.

The smallest proposed M2 workload is one immutable AVM package containing
`start/0` and only its reachable AtomVM library modules, one scheduler, one
reactor, no external networking/filesystem/native loader. Support output via
a bounded console path and a real timed `receive` probe before declaring
event integration correct. Produce uncompressed compatible BEAM input or
explicitly include a bounded decompressor; do not silently assume host zlib.

## C library decision

| Candidate | Benefit | Cost / requirement | Decision |
| --- | --- | --- | --- |
| Project-specific freestanding library | Small initial shim; complete control | Implementing formatting, conversion, math and locale/reentrancy grows into a libc maintenance burden | Limit custom code to native bindings/startup/allocator glue, not a full libc |
| Picolibc 1.8.12 | AArch64 support, configurable TLS/threading/malloc, maintained string/math/stdio facilities | Meson cross-build and generated headers; console hooks, exit and allocation integration; defaults assume TLS | Recommended initial candidate: no TLS, single-thread, external allocator, console-only stdio |
| Newlib | Mature embedded libc; explicit reentrant covers and allocator hooks | Per-thread `_reent`/errno and locking policy, syscall-shaped OS glue, possible broader configuration surface | Viable fallback if the Picolibc build/profile fails; no evidence it solves TLS or ERTS requirements automatically |

Picolibc's pinned `doc/os.md` explicitly describes non-POSIX integration and
simple `FILE` callbacks; `meson_options.txt` supplies `single-thread`,
`thread-local-storage`, `enable-malloc` and stdio controls. Initial intended
values are `single-thread=true`, `thread-local-storage=false`,
`enable-malloc=false` with Knytos providing one allocator facade. They are a
design selection, not a qualified cross-build command. Meson is not installed
on this host, and neither candidate libc was built in M0. Newlib's
[upstream manual](https://sourceware.org/newlib/libc.html#Syscalls) documents
the reentrant OS covers and malloc locking/reentrancy requirements.

M1 must compile and link the real required libc subset before accepting this
choice. Unsupported descriptor/filesystem/process functions fail the build
or return documented errors through deliberately exposed APIs; they must not
report success. No `fork`, `exec`, fake PID, fake UTC or ambient file namespace.

## Measured limits and resource gates

| Constraint / observation | Value | Meaning |
| --- | --- | --- |
| Runtime heap | 4,194,304 bytes default; reserved VA window ceiling 5,177,344 bytes | `HEAP_SIZE` / `HEAP_VA_LIMIT`; adaptive sizing is not an unrestricted growable C heap |
| Signed artifact ELF | 4,194,304 bytes maximum | Includes embedded modules and ELF/signature overhead; also check PT_LOAD addresses against reserved pages |
| User stack | 16 KiB default, up to 256 KiB configured; initially one committed page | Stack faults may grow within the admitted limit; a C library may not assume a multi-megabyte pthread stack |
| Threads | 16 default, 64 hard limit | Every scheduler/dirty/I/O/helper thread counts; signed deployment can select within the limit |
| Independent shard CQs | Four plus default CQ | Not one private CQ for each of 64 possible threads; an SMP waiting contract is required |
| Host AtomVM binary | 571,928 bytes, Darwin arm64 `MinSizeRel` | Host executable with Unix adapter and dynamic libraries; not native ELF/package or static libc size |
| Host ERTS sample | 28,503,288 bytes reported VM memory, including 5,568,783 code bytes | Installed ERTS 17.0.3 / OTP 29 with `+S 1:1 +SDcpu 1:1 +SDio 1 +A 1`; not a target minimum or the inspected 29.1.2 build |

The compile-only [ABI probe](evidence/erlang-m0/abi-probe.c) produced a
70,496-byte ELF with machine 183, type 2 and entry `0x20000`; its RX load is
20 bytes and its RW BSS is eight bytes. Headers/padding dominate the file.
It validates compiler/linker availability and C11 LP64/lock-free pointer
assertions, not the libc or Rust/C calling boundary. The
[TLS probe](evidence/erlang-m0/tls-probe.s) emits a `TPIDR_EL0` read.
The kernel has LP-base helper definitions for that register, but a repository
search did not establish active callers of those helpers. The decisive gap
is absence of user TP initialization/save/restore and TLS image handling;
do not infer a proven live register collision from helper names alone.

M1 is a conditional **go** for small TLS-free C executables. M2 target fit
remains **unmeasured**, with no source-level blocker identified for the bounded
single-scheduler configuration. Measure signed native ELF plus AVM package,
heap peak and stack high-water before M2 acceptance. ERTS is **not qualified**
for the default arena: the host observation exceeds it, and there is no native
measurement. It will need a separately owned mapping/carrier strategy or a
reviewed layout change, not merely a larger `malloc` constant.

## Contemporary ERTS and historical OSE

OTP 29.1.2 inspection confirms separate OS and thread-library work:

- `erts/configure.ac` requires a thread library; one normal scheduler does not
  remove native threading. `erts/lib_src/pthread/ethread.c` is a real pthread
  backend, including stack attributes and TSD. A Knytos backend must implement
  required thread/event/TSD/lock semantics, or justify a narrowly scoped shim;
  a new `sys/knytos` directory alone is insufficient.
- `erts/lib_src/common/ethr_aux.c` establishes stack minima; `erl_process.c`
  and thread-progress machinery depend on correct synchronization and deferred
  reclamation. Measure actual deepest stack demand, count helper threads and
  test shutdown before choosing deployment limits.
- [`erl_cmd.md`](https://github.com/erlang/otp/blob/d199a49a01a2079afb6f2c5c9ae047cd4555e68d/erts/doc/references/erl_cmd.md)
  documents dirty I/O schedulers separately; their default is ten and the
  minimum is one. Use an explicit bounded configuration when measuring; do not
  interpret `+S 1` as a one-thread VM.
- `sys/common/erl_mmap.c`, `erl_mseg.c` and the allocator layer distinguish
  mapping, alignment, discard and memory protection. Owned Knytos memory
  objects provide backing, but not every virtual-memory semantic. Interpreter
  operation avoids JIT authority, not allocator or guard-page requirements.
- `sys/common/erl_poll.c` / `erl_check_io.c` and Unix `sys.c`, `sys_time.c`,
  `sys_drivers.c`, `erl_child_setup.c` expose event, clock, driver, filesystem,
  environment, signal and external-process dependencies. Determine mandatory
  bootstrap pieces and exclude optional ones; do not stub all Unix imports.
- Host bootstrap tools, `erts/preloaded`, `lib/kernel` and `lib/stdlib` must
  match the target release. Authenticated archive/module lookup is distinct
  from native dynamic-library loading. Static NIF/driver registration needs
  investigation in the selected build, not copying AtomVM registration code.

OTP 18.3 has actual `erts/emulator/sys/ose/{sys,sys_time,erl_poll}.c` and
`erts/lib_src/ose/{ethread,ethr_event}.c` boundaries. The inspected poller uses
OSE receive/semaphore facilities and the thread backend depends on OSE/EFS
headers and signal structures. This confirms a non-Unix backend precedent;
those implementations cannot be transplanted to modern Knytos or used as a
contemporary runtime baseline. Historical source identities are retained in
the evidence summary.

Alternative distribution is a later userspace integration. The official
[carrier documentation](https://www.erlang.org/doc/apps/erts/alt_dist.html)
supports process distribution controllers, so M7 should compare that approach
with a static native driver before creating another driver ABI. Authentication,
connection ordering, controller lifetime and incarnation checks remain needed.

## Refined implementation sequence

The next scope is M1 only, after this report is reviewed:

1. Establish a reproducible C11/LLD build using `aarch64-none-elf`, the existing
   static ELF layout and a Rust-owned entry wrapper. Record compiler/runtime
   builtins and reject unresolved imports, dynamic relocations and TLS use.
2. Cross-build the proposed Picolibc subset; integrate one allocator facade,
   bounded output, monotonic clock and explicit unavailable UTC. Register
   constructor/finalizer arrays deliberately; define normal return and abort.
3. Build/sign a C application through the existing artifact/descriptor path.
   Keep `Context` bootstrap authority borrowed. Add native C↔Rust ABI probes
   for integer/pointer/struct/floating-point/variadic conventions as applicable.
4. Execute hello, allocation/failure/reallocation/alignment, formatting,
   startup/constructors, clock and exit probes. TLS is an expected build
   rejection. Prove a C failure does not break the retained Rust baseline.
5. Publish measured ELF/heap/stack values and rejection/cleanup results. Stop
   for M1 review before adding AtomVM, thread synchronization or kernel work.

Proposed organization: `crates/knytos-cabi` as a root-workspace `staticlib`,
`ports/libc` for pinned source acquisition/configuration, C headers and native
fixtures under the port tooling, and a separate `ports/atomvm` adapter in M2.
The existing Rust clients remain the implementation behind service bindings;
do not add `knytos-services` as a dependency of the generic C runtime.

Refined M2 prerequisites are the independently pinned OTP 27.3.4.19 host toolchain,
an authenticated minimal module/package closure and the measured C profile.
Implement actual `sys.h` hooks, static NIF registration, monotonic timer waits
and bounded module lookup. Validate a timed receive and wake-before-wait along
with hello before runtime startup is considered qualified. Leave SMP and
general native service bindings to M3/M4.

## Acceptance and remaining work

M0 is complete as a discovery deliverable: the baseline was built/executed,
actual interfaces and pinned upstream boundaries were inspected, libc options
compared, constraints measured or explicitly classified, and M1/M2 refined.
This is not acceptance of native runtime implementation.

| Gate | Disposition |
| --- | --- |
| Existing Knytos baseline and relevant host tests | Passed, scope above |
| Pinned AtomVM/ERTS/OSE source investigation and dependency matrix | Completed; file hashes retained |
| C compiler/linker availability | Passed structural probe; native ABI execution deferred to M1 |
| Libc choice | Picolibc recommended conditionally; cross-build and required-symbol qualification in M1 |
| Native TLS/SMP | Unsupported by the initial profile; generic design required before M3/M5 |
| AtomVM compiler compatibility | OTP 27.3.4.19 selected; acquisition/build and compatible BEAM execution outstanding before M2 |
| AtomVM target package/heap/stack fit | Unmeasured; required before M2 acceptance |
| ERTS target fit and native bootstrap | Unqualified; dedicated design and measurements required before M5 |
| Physical hardware, x86 C ABI and multi-node execution | Not executed in M0 |
| M1 authorization | User reviewed M0 and authorized M1; see [implementation scope](erlang-m1-implementation.md) and [M1 results](../reports/milestones/2026-11-24-c-runtime.md) |
