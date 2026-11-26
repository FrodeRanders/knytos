# Milestone M4 — Completion report

## Summary

M4 passed for the signed single-scheduler AtomVM service profile. An Erlang
application can discover and invoke an authorized native Knytos service,
exchange scalar and binary results, and observe endpoint closure. The
qualified configuration uses one Erlang scheduler and one native worker on
a two-CPU AArch64 guest.

## Implemented functionality

The bundled `knytos` module provides asynchronous requests, synchronous
calls with selective receive, cancellation, lookup, explicit handle close,
one-shot close subscriptions and diagnostics. The
[service API](../../../ports/atomvm/README.md#scoped-native-service-calls)
documents arguments, reply terms and overload behavior.

## Architecture decisions

The scheduler owns Erlang terms, recipient identity and mailboxes. The
worker owns the Rust connections, pending IPC and native cleanup. Process
exit, cancellation and receive timeout can withdraw delivery interest.
Native custody continues until settlement. The profile admits one request,
one reusable handle, one close watch and eight queued or reserved native
deliveries across the VM.

## Modified components

The platform adapter and bundled Erlang module live in
[`ports/atomvm`](../../../ports/atomvm/README.md). The
[`knytos-cabi` worker](../../../crates/knytos-cabi/src/service_worker.rs)
reuses the application grant client and Rust IPC owners. The
[signed runner](../../../ports/libc/run-tests.py) deploys the AtomVM
fixtures and their native Rust peer.

## Upstream dependencies and patches

The profile uses AtomVM `ff993a80963298b532c1e573f883951ecaac9fef`, OTP
27.3.4.19 for BEAM compilation, and Picolibc 1.8.12. The build generates
the target scheduler and mailbox adaptations from the pinned source.
It records both source hashes and the signed qualification records
the resulting artifact identities.

## Build procedure

Set `ATOMVM_SRC`, `OTP27_SRC`, and `PICOLIBC_SRC` to the pinned source
trees. Run from the Knytos checkout with a built OTP 27 host compiler
and a new validation output directory.

```sh
out=target/atomvm/m4-completion
for fixture in native_service active_cancel shutdown_service; do
  python3 ports/atomvm/build.py --atomvm-source "$ATOMVM_SRC" \
    --otp-source "$OTP27_SRC" --picolibc-source "$PICOLIBC_SRC" \
    --service-worker --application "ports/atomvm/tests/$fixture.erl" \
    --output "$out/$fixture.elf" || exit
done
python3 ports/libc/run-tests.py --picolibc-source "$PICOLIBC_SRC" \
  --atomvm-service-elf "$out/native_service.elf" \
  --atomvm-active-cancel-elf "$out/active_cancel.elf" \
  --atomvm-shutdown-busy-elf "$out/shutdown_service.elf" \
  --atomvm-node-shutdown --smp 2 --output "$out/run"
```

## Test execution and results

The final QEMU 11.1.3 campaign passed 14 signed cases and three rejection
controls. The client received an endpoint-close event with seven unread
replies, reacquired the replacement after ASID reuse and joined its
worker before C runtime exit. Node shutdown emitted QMP `guest-shutdown`
and QEMU exited with status zero. The
[run summary](../../plans/evidence/erlang-native-backpressure/summary.json)
and [serial excerpts](../../plans/evidence/erlang-native-backpressure/serial-excerpts.txt)
retain the final run evidence.

## Acceptance criteria

| Criterion | Disposition | Observation |
| --- | --- | --- |
| Discover an authorized native service | Passed | Signed lookup returned a reusable handle. An unlisted grant was rejected. |
| Invoke the service | Passed | Named and bound calls returned scalar and binary results. |
| Deliver native replies asynchronously | Passed | The scheduler delivered a result tagged with the submitted reference. |
| Keep unrelated Erlang processes progressing | Passed | Erlang timers and process work progressed with an occupied native slot. |
| Define and test cancellation and timeout | Passed | Grant cancellation, accepted-call cancellation, owner exit and both deadline paths were exercised. The server rejected late replies to cancelled calls. |
| Preserve opaque, scoped capabilities | Passed | Foreign, serialized, closed and replaced handles were rejected. |
| Bound resources and report overload | Passed | Requests, watches and mailbox deliveries hit their admission bounds and recovered on the expected release paths. |
| Interoperate with native Rust | Passed | The signed Rust fixture echoed binary payloads and exposed endpoint closure through the same client. |

## Security considerations

The signed runs cover grant denial, malformed and oversized inputs,
unexpected returned attachments, resource release, and stale or foreign
references. The replacement fixture reused the retired service's ASID
and verified that old handles and watches stayed stale. These are scoped
checks of the Erlang/native adapter. General kernel memory isolation is
tested through the kernel's existing isolation and ownership suites.

## Performance observations

Performance tuning remained paused for M4. The
[M3 scheduler measurements](2026-11-25-atomvm-scheduler-performance.md)
remain historical baseline data.

## Known limitations

The native service profile uses a single Erlang scheduler. The fixed
two- and four-scheduler AtomVM builds have separate qualification.
General capability attachments, additional event types and Erlang
distribution are outside the qualified M4 profile.

## Unresolved issues

The AtomVM service worker and mailbox admission rely on sole-scheduler
ownership. Combining native services with the fixed SMP profile requires
a concurrent ownership and teardown design.

## Regression assessment

The final signed campaign also reran the C ABI fixtures, native
diagnostics, synchronous calls and busy-service shutdown. Separate
qualification records retain the [mailbox race](2026-11-26-atomvm-synchronous-calls.md)
and [diagnostic bounds](2026-11-26-atomvm-native-diagnostics.md).

## Recommended next steps

Use the qualified AtomVM profile for local native-service experiments.
The programme's conventional OTP application objective still requires
official ERTS. Treat the ERTS backend as a separate port with a reviewed
thread, allocator, I/O and authenticated bootstrap contract.

## Proposed scope of next milestone

The first M5 increment should establish an honest AArch64 configure and
toolchain path, and qualify a small native thread/event/TSD test against
the resulting SDK before attempting ERTS boot. Knytos thread ownership,
join, event readiness and stack admission need to hold for every thread
in that test. ERTS TSD and compiler-generated ELF TLS have separate
requirements.

### ERTS prerequisite probes

The preflight used the clean OTP 29.1.2 source at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`, the existing Knytos
worker-libc profile and Apple Clang 21.0.0. With `OTP29_SRC` set to the
absolute source path, the target probe ran in an empty build directory.

```sh
ERL_TOP="$OTP29_SRC" "$OTP29_SRC/erts/configure" \
  --srcdir="$OTP29_SRC/erts" \
  --build="$("$OTP29_SRC/make/autoconf/config.guess")" \
  --host=aarch64-unknown-knytos --disable-jit 'CC=clang --target=aarch64-none-elf'
```

It exited 1 during host canonicalization with
`Invalid configuration 'aarch64-unknown-knytos': OS 'knytos' not recognized`.

The C probes used `python3 ports/libc/build.py probe.c --worker-libc
--picolibc-source "$PICOLIBC_SRC" --output probe.elf` on each of these
independent sources.

| Probe source | Observed build result |
| --- | --- |
| Include `knytos.h` and return `knytos_clock_monotonic_ns(&ns)` from `main` with a local `uint64_t ns` | Exit 0. Unsigned AArch64 ELF SHA-256 `367753bbc0a6caa6ab8796ff94b63bf29745d06a80fe9414cc8d5a28b3b6fa78`. |
| Include `pthread.h` and call `pthread_key_create(&key, 0)` with a local `pthread_key_t key` | Exit 1. Clang reported `'pthread.h' file not found`. |
| Declare `static _Thread_local int value = 1` and read through its address with a volatile pointer | Exit 1. The object check rejected TLS. |

The installed host ERTS 17.0.3 reported one normal, dirty CPU and dirty
I/O scheduler, one async worker and one primary poll thread with
`+S 1:1 +SDcpu 1:1 +SDio 1 +A 1 +IOt 1 +IOp 1`. This was a host
reference run. The pinned source adds at least one auxiliary thread in
`erts_start_schedulers`. For those settings the pinned startup paths
therefore account for at least six spawned threads plus startup. The
[current C roster](../../../ports/libc/README.md#restricted-native-worker-profile)
supplies three worker slots plus startup.

The preflight establishes configure, thread API and TLS gaps in the
current profile. An ERTS target binary or guest boot has yet to be
qualified.
