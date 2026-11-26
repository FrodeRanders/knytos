# AtomVM synchronous Erlang calls — 2026-11-26

The signed service profile bundles `knytos:call/2,3`. The wrapper submits
one scalar or binary request and selects only its reply reference.
`call/2` uses the worker's native deadline. `call/3` accepts `infinity`
or an Erlang receive timeout in milliseconds. A finite timeout returns
after requesting cancellation and removing a matching reply that may
already be queued. Native settlement remains with the worker.

## Validation

The signed two-CPU AArch64 QEMU campaign passed 14 cases and three
rejection controls. The fixture exercised named and reusable-handle
calls, scalar and binary replies, immediate admission errors, and
invalid timeout rejection before submission. It also observed the
native timeout through `call/2`.

The service received the finite-timeout call and held its reply token.
Erlang selected the timeout, the request slot was reused within the
fixture's bound, and the service's late reply was rejected. An unrelated
reply remained available to the Erlang caller.

A separate pinned host AtomVM test substituted the request and cancel
stubs to force a matching reply into the mailbox as cancellation returned
`{error, not_found}`. The wrapper removed that reply and preserved the
unrelated message. The host Rust suite passed 77 unit tests and one
compile-fail doctest. Signed node shutdown completed with QEMU exit status
zero.

The [run summary](../../plans/evidence/erlang-native-synchronous-calls/summary.json)
records the signed artifacts and host race fixture. The
[serial excerpts](../../plans/evidence/erlang-native-synchronous-calls/serial-excerpts.txt)
show request acceptance, the Erlang timeout, and rejection of the late reply.
