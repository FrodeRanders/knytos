# Native ERTS monotonic clock prerequisite

Observed 2026-11-27 on `feature/erlang-runtime`, based on
`1c0e8bd05506ed9deec3a6064ec14bdd9f369fba` plus the source hashes in the
[qualification manifest](../../plans/evidence/erts-monotonic-clock/summary.json).
The target remains OTP 29.1.2,
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d`.

## Result and boundary

The independent native clock archive implements `erts_os_monotonic_time`
and `erts_sys_hrtime` as signed-nanosecond reads through the existing scalar
Knytos monotonic query. Successful reads have no allocation, RPC, wait or
mutable clock state. The native C bridge reads kernel counter/frequency
scalars and converts through checked unsigned 128-bit arithmetic. The new
adapter additionally rejects values above `INT64_MAX`; neither OTP interface
can return an error, so source failure or signed-range exhaustion emits one
bounded diagnostic and aborts the domain. Failed logging still terminates.
The C abort bridge supplies the existing domain-abort path without C
finalizers or worker cleanup.

The archive exports exactly those two functions and imports only `abort`,
`knytos_clock_monotonic_ns` and `knytos_console_write`. It is built separately
from the ethread archive with `build.py --monotonic-clock`. Rebuilding the
default nine-member ethread archive reproduced its previously qualified
SHA-256, `0ef39934fc1a745557a38ad46300e8b969bd9f50a01a2361b70b116d5bdc9fba`.

This is a standalone prerequisite, not a selected ERTS platform backend.
No native sys header, configure selection, `sys_init_time` metadata, system
clock, paired clock read, performance-counter selection or timer-wheel
integration is supplied. The native configure probe is unchanged and remains
at its wall-clock-source gate. Reads rely on the kernel's monotonic-counter
contract; they do not establish frequency correction, security time or
comparability between machines. IRQ, signal and reentrant console contexts
are not qualified.

## UTC call-context finding

In the pinned `erts/emulator/beam/erl_time_sup.c`,
`get_not_corrected_time` calls `erts_os_system_time` while holding
`erts_get_time_mtx`. `os_system_time_0` and `os_system_time_1` also call it
directly from BIF implementations. The existing service-worker UTC query
can block for its one-second grant/query budget and may reject launch-profile
mapping admission as busy. It must not simply replace that statusless clock
getter. Native system time needs a local read design with explicit
synchronization, adjustment and failure policies. No cached UTC estimate or
implicit RPC retry was added by this work.

## Qualification

The [interface record](../../plans/evidence/erts-monotonic-clock/interface.json)
and [generated consumer](../../plans/evidence/erts-monotonic-clock/contract.c)
extract the actual LP64 typedefs and both function declarations from the
pinned OTP Git object. They check type compatibility and nanosecond units,
then retain and link both functions into a validated static AArch64 ELF.
Signature and unit mismatch controls both reject with their expected
diagnostics. This does not compile OTP's complete Unix or future native sys
header.

The same C implementation passed 28 host checks under undefined-behavior
sanitization: each function accepts zero, a fractional-second value and
`INT64_MAX`; each terminates on `INT64_MAX + 1`, `UINT64_MAX` and all nine
current nonzero C ABI statuses. The independent stub also rejects repeated
source reads and simulates failed diagnostic output. Terminal cases use a
host harness exit of 42; they are not guest domain-abort fault injections.

All 18 signed cases and three deployment controls passed on two guest CPUs.
The clock fixture starts one worker on LP 1 and reads both interfaces 256
times per participant while holding a native mutex. Each pair is bracketed
by direct C monotonic samples and checked for ordering and nondecreasing
values. The fixture joins the exact worker, releases its event/mutex owners,
checks ordinary-profile UTC remains unavailable and reports zero tracked C
blocks. The baseline expected-abort case and a later survivor also pass.
The [serial excerpts](../../plans/evidence/erts-monotonic-clock/serial-excerpts.txt)
retain the clock and survivor observations, including:

```text
ERTS MONOTONIC startup and worker locked nanosecond reads PASS
[c-runtime] exit=0 released_blocks=0 remaining_blocks=0 heap_peak=206 sampled_stack=512
```

Exact join and zero tracked C blocks do not prove physical root retirement.
These checks do not run an emulator or qualify all future ERTS clock callers.
The earlier [object-store extent incident](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
and both failed [C realtime worker captures](2026-11-27-c-realtime.md#failed-captures-and-admission-qualification)
remain recorded separately; this passing campaign does not close them.

| Artifact | SHA-256 |
| --- | --- |
| One-member native clock archive | `a38d7a114420ef7676247e2311f35eaf335a58823377061169c4b27c3766d184` |
| Interface link ELF | `b75a240f0003ebd0dfe0081b48c7800d3abf051f8329d10ac47e7f87939904f8` |
| Clock fixture, unsigned | `a7a3701931f48e07dda5de1210a95e1346b9dcf37d7e056ba7dd736d35801c7f` |
| Clock fixture, signed | `982d439a1205da7a38a5fc9206db4743aa799a2f68494691779a6bb75d784903` |
| Kernel | `ee12bc2417de54577f60de14655962cbd43cdd8d6418de71afbab00dc6620136` |

The manifest includes reproduction commands, toolchain identities, source
hashes, deployment controls and fixture/descriptor identities. Its
[checksum list](../../plans/evidence/erts-monotonic-clock/checksums.json)
covers the version-controlled evidence.
