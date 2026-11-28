# M5 native clock configure selection

The next bounded integration step selects the existing native clock archives
in the pinned OTP 29.1.2 configure probe. This is a prerequisite for the full
native platform, not M5 completion.

**Status:** implemented and qualified within this boundary. Native clock
configure completes and emits its headers/Makefiles. See the
[results and remaining integration](../reports/milestones/2026-11-28-erts-clock-configure.md).
The follow-on [native platform routing step](erlang-m5-platform-routing.md)
now covers complete common-header compilation and clock-source Make routing.

## Discovery and boundary

The native ethread probe reaches `ERL_TIME_CORRECTION`, whose Unix-oriented
wall-clock search fails. That macro also probes CPU time. The standalone
Knytos backend already supplies local monotonic/high-resolution reads,
published UTC projection and startup metadata. Its UTC acquisition may block;
the getters must continue to use only published samples.

The startup adapter's normal include is `sys.h`; its qualified probe extracts
the actual common time record instead. Common `sys.h` still selects
`erl_unix_sys.h` for non-Windows targets, and the emulator Makefile still
selects Unix `sys_time.o`. Neither boundary is a native platform implementation.
This change must keep that distinction explicit. The existing finite sample
window is not a long-running refresh policy.

## Implementation and acceptance

1. Retain default and native-thread-only configure behavior. Run the current
   startup contract checker and native-thread-only configure as baselines.
2. Add an explicit native-clock mode requiring native ethread and a caller
   supplied bounded, nonzero sample age. Use the service-worker C SDK.
3. Stage the three-member archive and pinned time-record probe. A native
   Autoconf branch must genuinely compile/link retained references to all
   clock/startup entry points before selecting native clock definitions.
   Supply no successful POSIX clock or CPU-accounting feature answers.
4. Validate the actual link ELF and reject absent archive, mismatched policy,
   invalid target/backend/profile and conflicting clock choices. Record the
   next actual configure gate without bypassing it.
5. Preserve version-controlled evidence and update the current plan/README.

Native sys-header selection, emulator source routing, startup context,
notification-backed refresh ownership, CPU accounting, timer integration and
full emulator execution remain later integration gates. No kernel, runtime
owner or clock implementation changes are planned in this step.
