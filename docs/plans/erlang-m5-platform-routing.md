# M5 native platform header and clock source routing

This step replaces the extracted time-record boundary with the complete pinned
common `sys.h`, selecting a minimal explicit Knytos platform header and routing
the three existing clock objects through the emulator Makefile. It does not
implement the rest of the native platform or a refresh owner.

**Status:** qualified within this boundary. The generated clock Make rules,
complete common-header consumer and 20-case signed two-CPU campaign pass.
See the [qualification and remaining gates](../reports/milestones/2026-11-28-erts-platform-routing.md).

## Discovery

The preceding native thread/clock configure probe generates its headers, but
common `sys.h` unconditionally selects `erl_unix_sys.h` for non-Windows builds.
That path requires unavailable `netdb.h` even when compiling the existing
startup adapter. It also introduces Unix signal, descriptor, CPU-time and
performance-counter definitions that do not describe this native backend.
The configured emulator still uses the Unix object list and `sys_time.o`.

The new header will supply only actual C/clock prerequisites of the complete
common header and qualified time adapters. Common declarations remain upstream;
unimplemented native operations must not receive fabricated success stubs.

## Acceptance boundary

- Add explicit opt-in native platform selection, requiring native ethread and
  clock selection. Default and thread/clock-only modes retain their boundaries.
- Compile the complete pinned common `sys.h` with generated configuration and
  selected native ethread headers, without the time-contract probe, Unix sys
  header, POSIX feature overrides or substitute common/thread headers.
- Route native clock sources in the generated emulator Makefile. Exercise the
  selected build rules with an SDK that works from that Make working directory,
  and link a real consumer against the resulting archive.
- Check layout/types against the prior pinned time record; reject conflicting
  platform/profile/probe choices. Preserve the finite age/terminal behavior.
- Test relevant host/native boundaries and record the next real integration
  gates. Update the current plan/README and retain qualification evidence.

Full emulator startup, I/O, CPU accounting, stack bounds, floating-point
qualification and continuous notification-backed clock refresh remain open.
No new capability owner, native worker or kernel facility is planned here.
