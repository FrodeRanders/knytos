# AtomVM accepted-call cancellation — 2026-11-25

`knytos:cancel/1` now wakes the native worker while it waits for an
accepted scalar call. The worker binds reply readiness to its private
completion queue and checks cancellation and reply state after each wake.
It closes the exact pending call before releasing the connection and
returning the slot to the Erlang scheduler. Grant acquisition and call
admission still use the original one-second budget. Cancellation can
suppress reply delivery while remote effects persist.

## Validation

The signed two-CPU AArch64 QEMU campaign passed 14 cases and three
rejection controls. The signed `m4.hold` service received a call and held
its reply token. After the Erlang caller cancelled the call, the kernel
rejected the service's late reply and the service responded to the replacement.
The replacement reply arrived 134 ms after the original submission,
within the fixture's 750 ms bound. The cancelled reference delivered no
Erlang message. A second held call expired on the original deadline and
its late reply was rejected as well.

The regression fixtures covered grant denial, returned attachment
disposal, independent Erlang timers and signed shutdown. The VM joined
its service worker and acknowledged shutdown. QMP reported
`guest-shutdown` and QEMU exited with status zero. The host run passed
75 unit tests and one compile-fail doctest.

The [run summary](../../plans/evidence/erlang-native-active-cancellation/summary.json)
records the signed artifact identities and grants. The
[serial excerpts](../../plans/evidence/erlang-native-active-cancellation/serial-excerpts.txt)
show the ordered events across both deployments.
