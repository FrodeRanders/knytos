# M5 native UTC calendar and time-supervisor integration

The bounded native-time step compiles the common time supervisor with real native UTC
calendar reads and an explicit unsupported CPU-runtime statistic. It does not
add CPU accounting by substituting monotonic or wall-clock time.

Qualified on 2026-11-28: whole common time-supervisor/BIF compilation, eight
host cases, 21 signed cases and three deployment controls on two guest CPUs
pass. See the [results and artifact identities](../reports/milestones/2026-11-28-erts-time-supervisor.md).

## Discovery and decisions

Knytos records scheduler runtime ticks, but the inspected counters are not a
retired-generation-preserving domain total with an ERTS-compatible user/kernel
split. `erts_runtime_elapsed_both` is called only by `statistics(runtime)` in
the pinned emulator sources. The native port will reject that BIF with
`notsup` and exclude the unused CPU reader and its mutex, rather than publish
fabricated measurements. Other platforms retain the original implementation.
The test-only `proc_sig_block` hook also rejects on native targets until its
blocking contract is ported; it must not bring Unix `select` into this profile.

The initial headless native profile uses UTC for local calendar time, with no
DST or ambient TZ environment. Current calendar reads use the already-published
authorized UTC clock and reentrant `gmtime_r`; no blocking query occurs in
calendar getters. The common validated local/universal conversions are
identity conversions under this explicit UTC policy. Pure upstream Gregorian
conversions remain in common code.

## Acceptance

- Add an explicit native-time opt-in after native metadata selection.
- Compile the whole patched time supervisor with complete upstream headers and
  actual generated metadata, with no CPU/POSIX success stubs.
- Build the calendar adapter through generated Make rules. Verify unchanged
  common calendar function interfaces and the native BIF's unsupported branch.
- Test actual selected common calendar code, including leap days, pre-epoch
  values, year boundaries, invalid inputs and UTC/DST policy. Use section GC to
  link only that surface for C fixtures; do not replace unrelated ERTS code.
- Test preparation errors with unchanged output and terminal statusless errors,
  and signed startup/worker/missing-grant controls with the new adapter.
- Preserve failed captures separately, update current status and record the
  remaining full-runtime startup, refresh, CPU and I/O gates.

Common calendar execution in C fixtures is not an Erlang BIF/runtime boot test.
`statistics(runtime)` remains unsupported until a native accounting contract
exists. Full time correction, timer-wheel and native startup execution remain
unqualified.
