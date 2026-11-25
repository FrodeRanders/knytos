# AtomVM endpoint-close subscriptions — 2026-11-26

`knytos:subscribe(Handle, closed)` registers a one-shot watch against
the endpoint named by a reusable service handle. The client receives
an asynchronous registration reply and a separate `{knytos_event,
Reference, closed}` when the endpoint closes. The event makes the
handle stale. One watch can remain armed while the request slot serves
other native operations.

The worker registers and cancels the owned completion. The scheduler
observes the owner at its CQ-zero checkpoint and rechecks it before
parking. The custody lock protects only transfer of the completion.
Unsubscribe, service close, garbage collection and owner exit detach
Erlang delivery before the worker releases native resources.

## Validation

The signed two-CPU AArch64 QEMU campaign passed 14 cases and three
rejection controls. The fixture checked single-watch admission,
owner-scoped registration and unsubscribe, subscriber exit, and reuse
after cancelling a pending registration or an armed watch.

The Erlang client received a scalar acknowledgement and then waited
for the close event without an Erlang timeout. The service closed its
endpoint afterward. The native completion woke the VM and delivered
one event for the current watch. The old references stayed stale and
the handle rejected subsequent requests.

The shutdown fixture kept an armed watch while the worker served a
separate request. Startup joined the worker and acknowledged shutdown.
QMP reported `guest-shutdown` and QEMU exited with status zero. The host
suite passed 75 unit tests and one compile-fail doctest.

The [run summary](../../plans/evidence/erlang-native-close-watch/summary.json)
records the signed artifacts. The [serial excerpts](../../plans/evidence/erlang-native-close-watch/serial-excerpts.txt)
show the client waiting before endpoint closure and event delivery.
