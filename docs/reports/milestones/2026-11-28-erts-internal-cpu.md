# Native ERTS internal libraries and CPU pre-early initialization

Observed 2026-11-28 on `feature/erlang-runtime`, based on
`d55ac954d7e5e526b5749e620133a6f7b1285f26` plus the final source identities in
the [manifest](../../plans/evidence/erts-internal-cpu/summary.json).

Generated upstream Make rules build regular and reentrant ERTS internal
archives and the common CPU-topology object. Signed C fixtures execute the
unchanged common CPU pre-early prefix before native thread/clock pre-init,
with explicit unsupported CPU discovery. This is not common emulator
`early_init`, later CPU/topology initialization, scheduler execution or an
ERTS emulator boot.

## Reproduce

```sh
python3 ports/erts/configure.py --native-ethread --native-clock --native-platform \
  --native-metadata --native-time --native-pre-init --native-internal \
  --clock-sample-age-ms 5000 --otp-source /path/to/otp29 \
  --picolibc-source /path/to/picolibc --output /fresh/native-internal
python3 ports/libc/run-tests.py --erts-time-init \
  --erts-platform-build /fresh/native-internal --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/native-internal-validation
```

Use fresh paths without whitespace. OTP remains 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`; Picolibc remains 1.8.12 at
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. The
[scope and acceptance](../../plans/erlang-m5-internal-cpu.md) define this step.

## Real archives and link boundaries

The mode stages the genuine configured internal-library Makefile and builds
`liberts_internal.a` and `liberts_internal_r.a`. Each contains exactly
`erl_printf_format.o`, `erl_printf.o` and `erl_misc_utils.o`. The corresponding
three C sources and common `erl_cpu_topology.c` match the pinned Git blobs
exactly; their blob/SHA-256 identities are recorded. The regular and reentrant
archives happen to have identical bytes in this native profile. Both are
built separately and exercised independently.

The native Make changes add function/data sections to these internal objects
and the common CPU-topology object. Section GC retains the real selected
functions, without substituting common ERTS symbols. The positive consumers
link common CPU pre-early initialization and buffer formatting with each
archive, and pass the SDK's native ELF checks. Omitting either archive rejects.
Explicitly selecting the unported sleep or descriptor stream formatter still
fails on `select` or `write`. No POSIX success stub is added.

There are no unresolved strong symbols in the positive consumers. The existing
Picolibc stack-guard constructor retains optional weak `getentropy`; the
allowlist admits only that exact weak import. Its absence uses the previously
qualified non-secret guard fallback. This change supplies no entropy guarantee.

## CPU state, allocation and execution

All 29 signed cases and three deployment controls passed on two guest CPUs.
Both positive library consumers, four archive/helper rejection controls and
prerequisite-mode rejection also passed. Eight host calendar cases and the
preceding native startup/clock controls passed again in the final configure
and guest runs.

No native provider currently supplies CPU counts, quota, topology or affinity.
The common CPU-info getters return `-ENOTSUP`; they do not invent a count from
thread capacity or the configured guest CPU count. The initial one-scheduler
policy is a separate choice made by the common startup fallback.

Fixtures exercise real CPU-info create/update/destroy operations, unsupported
getters and binding probes, null arguments where the API checks them, and
unchanged topology output when no topology exists. Private CPU-info objects
are released exactly once. A test-only `--wrap=malloc` link intercepts one
allocation at a time; successful calls delegate to the real allocator. The
SDK records additional linker arguments in the unsigned build manifest.

Successful common `erts_pre_early_init_cpu_topology` installs a runtime-lifetime
CPU-info object. Its outputs report unsupported discovery and valid grouping
limits. The fixture then invokes native `erts_sys_pre_init`, completes late
thread initialization and runs a real worker. Both threads exercise common
buffer formatting, including signed/unsigned integer limits, padding,
truncation and zero-size rejection. Worker CPU-info objects are private.
Exact join and event teardown precede C runtime exit, which releases the one
remaining runtime-lifetime CPU-info block. There is no public common topology
shutdown API, so the fixture does not manufacture one or free its static owner
while the runtime could still use it.

Malloc rejection is exercised for both direct CPU-info construction and common
CPU pre-early initialization. The latter produces `-EINVAL` outputs with no
live CPU-info block. Its fixture stops before native pre-init and later CPU
initialization. Full startup still needs a terminal policy for this rejection:
the later common initializer requires a nonnull CPU-info object. Continuing
that unported full startup path is not qualified by these tests.

The [guest result](../../plans/evidence/erts-internal-cpu/guest-result.json)
retains each domain's observation. Focused build manifests record library,
source, unsigned-image and link-map identities; the manifest records signed
image/descriptor, kernel, firmware and boot-image identities. Existing native
clock/calendar/pre-init cases are repeated in the same two-CPU campaign.

| Artifact | SHA-256 |
| --- | --- |
| Regular/reentrant internal archives, each | `ef639e20a4a9a2ca5d16b81e60d6aeeefd529b000656a438ff46675176e02f49` |
| Common CPU-prefix archive | `8a7d793532cfee98e7e9498b89d07f867992fe66f22fda83d90cbc4abd89d718` |
| Guest kernel | `f4ad7b2638e906f6fa74018eed6d4f798e374053fd4913f0194240d7cc90536a` |
| Boot image | `9fff457b1f56ea54f0d57e6cfa4610c5019f54d265b530c9bda86c7ddc83735f` |

The [checksums](../../plans/evidence/erts-internal-cpu/checksums.json) cover
retained evidence, preserved byte for byte through scoped Git attributes.
Final source identities, Python syntax and staged whitespace checks pass.
No Rust source changed.

## Development captures and remaining work

The [monolithic baseline link](../../plans/evidence/erts-internal-cpu/baseline/link.txt)
failed on `select` and `write`, despite successful compilation and archive
creation. Native section flags resolve the selected-prefix linkage while
the explicit unsupported-consumer controls preserve those gates. Whole
internal archives are not claimed to implement every utility they contain.

The first positive links also passed ELF checks, but an over-strict diagnostic
check rejected the SDK's existing weak entropy hook. Its helper and
[symbol capture](../../plans/evidence/erts-internal-cpu/initial-weak-check/undefined.txt)
remain separate from the final precise allowlist. Intermediate successful
configure runs preceded the final allocation-failure fixture boundary and
descriptor-stream control. No guest execution is attributed to those runs.
Complete original orchestration stdout and every intermediate helper revision
were not retained. Final evidence identifies the qualified sources.

Native CPU discovery/binding, later topology/allocator/lock initialization,
CPU-info allocation-rejection terminal policy, descriptor stream formatting,
sleep, actual stack bounds and sufficient thread capacity remain open. So do
continuous refresh, I/O readiness, later platform hooks, timer/correction
execution, full dependency closure and a matching host Erlang bootstrap with
preloaded modules. No kernel or capability cleanup contract changed. The
existing C allocator owner and post-worker runtime exit are reused; a new
CPU-info object does not create a new cleanup owner family.
