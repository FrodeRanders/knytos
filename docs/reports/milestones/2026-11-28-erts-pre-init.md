# Native ERTS platform pre-initialization

Observed 2026-11-28 on `feature/erlang-runtime`, based on
`e53852feca9bf730e00d34d6b3272987ed7b131a` plus the final input identities in
the [manifest](../../plans/evidence/erts-pre-init/summary.json).

The actual native `erts_sys_pre_init` entry point now initializes native
ethread and invokes unchanged common `erts_init_sys_time_sup`. This executes
the common clock-metadata initializer with the native time record. It does
not execute common emulator `early_init`, the later correction/timer
initializer or a complete ERTS emulator.

## Reproduce and contract

```sh
python3 ports/erts/configure.py --native-ethread --native-clock --native-platform \
  --native-metadata --native-time --native-pre-init --clock-sample-age-ms 5000 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/native-pre-init
python3 ports/libc/run-tests.py --erts-time-init \
  --erts-platform-build /fresh/native-pre-init --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/native-pre-init-validation
```

Use fresh paths without whitespace. OTP remains 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`; Picolibc remains 1.8.12 at
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. The
[scope and acceptance](../../plans/erlang-m5-pre-init.md) define this step.
The opt-in requires native-time mode; preceding modes retain their behavior.

This early entry point is startup-only and one-shot. Native ethread owns the
identity/event-key transaction, including rollback on unstarted reservation
failure. A repeated call, worker call or preparation error terminates through
the existing bounded ethread fatal diagnostic. Clock authority/validation
failure after thread commit terminates the partial startup domain through the
existing clock diagnostic. No committed prefix is replayed or released early.

The initial headless profile is unbound. It installs no Unix signal-mask or
affinity-inheritance callbacks, opens no descriptors and creates no workers.
Native CPU binding remains unsupported. Lock instrumentation and signal-function
profiles reject at compilation until their startup contracts exist. Late
initialization remains separate and must succeed before worker creation.

## Actual build and common-code linkage

The generated emulator Makefile builds `erts_pre_init.o` and adds it to the
native platform selection alongside the four clock objects. The pre-init
archive is separate from the clock archive. Its symbol inventory retains only
the platform entry point, actual thread/common-initializer/diagnostic calls
and stack-protector dependencies. No Unix API or replacement common-code
success stub is imported.

The positive consumer links the pre-init archive, genuine common time-supervisor
object, Make-built clocks and qualified native ethread archive through complete
upstream headers. The captured ELF passes the native C SDK's layout checks.
Each of those four libraries is then omitted in turn; the corresponding real
dependency must fail linkage. Three lock-instrumentation profiles and signal
function selection must reject with the explicit native diagnostic. Missing
native-time selection rejects before source export.

The retained [common initializer](../../plans/evidence/erts-pre-init/common-initializer.c)
matches the pinned upstream function byte for byte. Generated metadata/YCF,
the four whole common-source compilations, earlier platform controls and eight
host calendar cases also pass in this configure run. The four-member clock
archive is byte-identical to the previous calendar qualification, while the
new entry point and combined execution require the new guest campaign.

## Signed execution

All 26 signed cases and three deployment controls passed on two guest CPUs.
The latter reject TLS, an unimplemented file API and an invalid signature.
All eight archive/profile controls and the prerequisite-mode rejection passed.

The campaign directly calls the actual platform entry point in signed C
deployments on two guest CPUs. Successful startup checks a cold clock and
zero common time unit before pre-init, then the published clock and common
nanosecond unit afterward. Worker creation before late initialization rejects
without changing the caller's output identity. Separate late initialization
admits the startup event, and a native ERTS worker runs with its automatic event
and initialized common metadata. Startup and worker exercise current calendar
reads, join exactly and release the worker/startup event owners. Normal exit
reports zero live C allocation blocks. Existing Gregorian calendar and clock
cases are repeated in the same campaign.

Four separate domains cover repeated pre-init, key pressure, missing clock
authority after thread initialization and a worker attempting pre-init. They
must emit the expected diagnostic and terminal domain abort. The worker case
retains the parent join owner while waiting; the parent does not assume that
creation-handshake completion means the worker entry has already run. A later
survivor checks continued node operation. Terminal runs are domain-abort tests,
not a claim that partial ERTS startup has a recoverable local shutdown API.

The [guest result](../../plans/evidence/erts-pre-init/guest-result.json) retains
per-case domain observations. The manifest records kernel, firmware, boot and
fixture identities. Focused fixture records retain unsigned build manifests,
signed image/descriptor hashes and admissions; the successful fixture's link
map is retained. No ERTS emulator image or Erlang BIF is executed.

| Artifact | SHA-256 |
| --- | --- |
| Native pre-init archive | `c2dee9f2ff18f5961a5a584570e4e720f4d8a3b70d44a416f3d0c852476538ff` |
| Make-built clock archive | `83edbd7b538840c40eeb7c6c41ca5b30d75bf3d5e89766631611ed9618bf6232` |
| Common time-supervisor archive | `648fc85377662e0d0d71979bd563134ac37b484957e89800e8abf164f67fec07` |
| Guest kernel | `b756fc31cf02e5b278a359b392b96cba0c6de24f5389c1a99a77acb74c6e32bc` |
| Boot image | `3f2d3316df8b77caeb558ccfa6b0980fe55f6b937ae40cebd409975de7539bfe` |

The [checksums](../../plans/evidence/erts-pre-init/checksums.json) cover retained
evidence. Final source identities, Python syntax and staged whitespace checks
pass. Scoped Git attributes preserve raw evidence bytes and upstream patch
context. No Rust source changed.

## Development capture and remaining gates

The first link succeeded, but its unsupported-profile compile control omitted
the generated metadata include path required by `erl_time.h`. It stopped on
missing `erl_alloc_types.h` before reaching the intended native guard. The
[diagnostic](../../plans/evidence/erts-pre-init/initial-control-includes/reject-ERTS_ENABLE_LOCK_COUNT.txt)
is retained separately. Final controls use the actual common-source compile
flags and pass. Complete initial orchestration stdout and the original helper
revision were not retained; this is a partial development capture.

Complete native startup still needs CPU topology, actual stack bounds,
sufficient thread capacity, later platform entry points, floating-point layout,
allocator/I/O integration, continuous refresh, timer/correction execution,
target internal libraries, the full dependency graph and matching host Erlang
bootstrap/preloaded modules. CPU-runtime statistics retain their explicit
`notsup` behavior. No kernel, capability-owner or cleanup contract changed;
the existing thread/bootstrap owners and terminal policies are reused.
