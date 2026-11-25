# AtomVM service cleanup on process exit — 2026-11-25

The service request slot now tracks the exact submitting Erlang
context. The scheduler detaches that context after releasing its
ready-list guard and before destroying the context or delivering monitor
notifications. The hook releases an unclaimed slot or signals the native
worker. The worker retains the accepted call and connection through
settlement outside the VM guards. Grant acquisition and call admission
remain bounded by the original deadline.

## Validation

The signed two-CPU AArch64 QEMU campaign passed 14 cases and three
rejection controls. The native fixture accepted and held two calls. In
each trial an unrelated Erlang process exited while the owner was alive,
and a probe still received `busy`. The owner then exited normally in
one trial and received a kill signal in the other. The kernel rejected
both late replies. Replacement calls succeeded 136 ms and 138 ms after
the original submissions, within the fixture's 750 ms bound and before
the one-second native deadline.

The campaign also exercised explicit cancellation, timeout, returned
attachment disposal and signed node shutdown. QMP reported
`guest-shutdown` and QEMU exited with status zero. The host run passed
75 unit tests and one compile-fail doctest.

The [run summary](../../plans/evidence/erlang-native-owner-exit/summary.json)
records the signed artifact identities and grants. The
[serial excerpts](../../plans/evidence/erlang-native-owner-exit/serial-excerpts.txt)
show the ordered events across both deployments.
