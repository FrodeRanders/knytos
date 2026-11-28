# Native ERTS UTC calendar and common time-supervisor qualification

Observed 2026-11-28 on `feature/erlang-runtime`, based on
`0bd66b0eadeb0a6a8abb0a89b9c5565730fde48d` plus the input identities in the
[evidence manifest](../../plans/evidence/erts-time-supervisor/summary.json).

The native-time profile compiles the whole common time supervisor and BIF
source using genuine generated emulator metadata and complete upstream
headers. Its current calendar getters use native UTC. CPU-runtime statistics
explicitly reject until native accounting exists. Selected common calendar
execution is qualified independently from full time-supervisor startup and
Erlang BIF execution; no ERTS emulator boot is claimed.

## Reproduce

```sh
python3 ports/erts/configure.py --native-ethread --native-clock --native-platform \
  --native-metadata --native-time --clock-sample-age-ms 5000 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/native-time
python3 ports/libc/run-tests.py --erts-time-init \
  --erts-platform-build /fresh/native-time --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/native-time-validation
```

Use fresh paths without whitespace. OTP remains 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`; Picolibc remains 1.8.12 at
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. The
[scope and decisions](../../plans/erlang-m5-time-supervisor.md) define this step.
Previous configure modes remain available without native-time selection.

## Native contracts

The inspected [scheduler total](../../../crates/catten/src/cpu/scheduler/threads/mod.rs)
retains retired CPU contributions at node scope. The exposed live thread/domain
statistics do not supply a retired-generation-preserving domain total or an
ERTS user/kernel split. The pinned common sources call
`erts_runtime_elapsed_both` only from `statistics(runtime)`. Native Knytos
therefore raises `notsup` for that statistic and excludes the CPU reader,
declaration and unused mutex. Monotonic and wall-clock time are not used as CPU
time. The original nonnative branches remain in place.

The initial headless profile defines local time as UTC, without DST or an
ambient `TZ` environment. The four common current-calendar getters call a
small adapter that reads the existing published authorized clock and converts
whole seconds through reentrant Picolibc `gmtime_r`. No service query occurs
in a getter. The adapter prepares a complete `struct tm` before output commit;
errors preserve the caller's object. Null/misaligned output, clock errors,
negative timestamps and conversion errors reject. The upstream statusless
getters log a bounded diagnostic and abort on failure, including stale/cold
clock state; failure of the diagnostic itself cannot turn that into success.

Native local/universal conversions validate through upstream
`univ_to_seconds` and preserve the supplied fields under UTC. DST hints are
ignored under this explicit no-DST policy. Pure common Gregorian conversions
retain their upstream implementation and supported year range. Current
published UTC is separately bounded by the signed nanosecond clock contract;
this does not extend that clock to every date supported by pure conversion.

The test-only `proc_sig_block` internal-state hook raises `notsup` on native
targets. Its upstream implementation blocks through Unix `select`; no fake
descriptor API, sleep loop or successful blocking stub is supplied.

## Build and execution boundaries

The generated emulator Make rules build four clock objects, including the
calendar adapter. Genuine upstream metadata generation and pinned host YCF
tests still pass. Full common `erl_term.c`, `erl_init.c`, `erl_time_sup.c` and
`erl_bif_info.c` compile to AArch64 objects with warnings treated as errors.
The [preflight commands](../../plans/evidence/erts-time-supervisor/common-preflight.json)
and generated configuration identify the complete include path and source
selection.

The time-supervisor object's symbol inventory contains no CPU reader or
POSIX CPU/calendar fallback imports. The whole actual BIF source is also
preprocessed with the same headers. Its
[runtime-statistic excerpt](../../plans/evidence/erts-time-supervisor/bif-excerpt.txt)
shows the exception branch, without a CPU-reader reference. This is compilation
and preprocessing evidence, not an executed Erlang exception test.
A negative compile rejects an attempted call to the excluded CPU reader;
native-time selection without metadata rejects at argument validation.

The C fixture links the actual common time-supervisor object from a separate
archive with function/data sections and section GC. It executes only its
selected calendar surface; unrelated ERTS functions are discarded, without
replacement success stubs. The common time-supervisor initializer, correction
logic, timer wheel, scheduler callbacks and BIF bodies are not executed.

Eight host cases run the calendar adapter with undefined-behavior sanitization.
They cover leap-day fields, weekday/year-day data, ambient TZ independence,
the signed nanosecond upper boundary, subsecond truncation, null/alignment
rejection before reads, unavailable/negative/conversion-failure output
preservation, successful statusless reads and terminal failure even when
logging fails. A controlled wrapper injects only the conversion failure;
successful cases delegate to the actual host `gmtime_r`. Native calendar
execution below uses the target Picolibc implementation.

## Signed guest campaign

All 21 signed cases and three deployment controls passed on two guest CPUs.
The deployment controls reject unsupported TLS, an unimplemented file API and
an invalid signature. Eight host cases and both additional native-time
selection/declaration rejection controls also passed.

The campaign uses the new Make-built four-member archive and actual common
calendar object, so earlier three-member clock qualification is not reused as
proof for this change. The runner requires the calendar success marker in the
same launched domain as the startup/worker and terminal markers.

The calendar fixture checks eight exact date/epoch vectors from 1600 through
2400, all 146,097 days of a Gregorian 400-year cycle against independent
calendar-day iteration, the upstream maximum accepted year, eleven invalid
dates/times with preserved outputs, and UTC identity conversions with DST hints
from -1 through 1. Startup and worker threads exercise all four current-calendar
getters against the local published clock and join exactly. Missing signed
authority preserves cold state and calendar output. A separate cold current
calendar getter must diagnose and abort. Normal fixtures finish with zero live
allocation blocks.

The [guest result](../../plans/evidence/erts-time-supervisor/guest-result.json)
retains each case's domain logs and outcome. The manifest records unsigned
fixture, signed image, deployment descriptor, kernel, firmware and boot-image
identities; the focused fixture records retain link maps and library hashes.
These are signed C deployments on a two-CPU guest, not deployments of an ERTS
emulator.

| Artifact | SHA-256 |
| --- | --- |
| Make-built four-member clock archive | `83edbd7b538840c40eeb7c6c41ca5b30d75bf3d5e89766631611ed9618bf6232` |
| Common calendar archive | `7bb87e09f7cd8a1701601c7f2abfc4e45085c6c9a506473197e07682f72c9cea` |
| Guest kernel | `c04fa40a819cca4342076780d604aa080d1af39b5ee27741a2991455071d4802` |
| Boot image | `2a46b66e0823e6964bc9f440e41463d6c212c4b24214a65a6ae2cc438c8ecd8c` |

The [checksums](../../plans/evidence/erts-time-supervisor/checksums.json)
cover the retained evidence. Final source identities, Python syntax and
`git diff --check` pass. Scoped Git attributes preserve captured evidence bytes
and upstream patch context without whitespace normalization. No Rust source
changed.

## Development capture and remaining work

The [initial BIF compile](../../plans/evidence/erts-time-supervisor/initial-bif-select/erl_bif_info.c.txt)
failed on `struct timeval` and `select` in the test-only `proc_sig_block` hook.
That run already compiled the time supervisor and passed its CPU-reader symbol
exclusion, but did not qualify whole BIF compilation. Its patch and result are
retained separately. The final explicit native `notsup` branch resolves that
compile gate. Complete orchestration stdout and every intermediate helper
revision were not archived; the retained initial capture is not a complete
history of development. Final evidence identifies the qualified source inputs.

Full native `erts_sys_pre_init`/startup execution, refresh scheduling and
notification ownership, I/O readiness, actual stack bounds, sufficient thread
capacity, floating-point layout, target internal libraries, the complete Make
dependency graph and matching host Erlang bootstrap/preloaded modules remain
open. CPU accounting, local timezone/DST support, timer/correction integration
and executing the unsupported BIF paths also remain unqualified. The five-second
sample policy still has no continuous refresh producer. No kernel resource,
capability-owner or cleanup contract changed; earlier incidents retain their
existing dispositions.
