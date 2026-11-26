# AtomVM grant cancellation — 2026-11-26

The native worker waits for accepted grant and service replies on the
same private completion queue. The scheduler publishes cancellation
before waking that queue. The worker settles the exact pending owner
before making the request slot available again.

Grant discovery still retries a confirmed unavailable reply within the
original one-second budget. The catalog has no publication notification
for grant clients. The existing bounded availability backoff now uses a
checked CQ wait so cancellation can end it early. Queue-admission retries
also check cancellation before submitting again.

## Validation

The signed two-CPU AArch64 QEMU run passed 14 cases and three rejection
controls. The fixture submitted a lookup for an authorized name with no
publisher and confirmed that a second request received `busy`. It then
cancelled the lookup. The replacement scalar reply arrived 58 ms after
the original submission. When a different lookup's owner exited, the
replacement arrived after 35 ms. Both trials met the 750 ms fixture bound.

The host suite passed 77 unit tests and one compile-fail doctest. Focused
tests verified that cancellation after CQ binding closes the exact pending
call and that cancellation after a queue-full rejection stops resubmission.
The signed suite also exercised accepted-call cancellation, same-name
service replacement, and clean node shutdown.

The [run summary](../../plans/evidence/erlang-native-grant-cancellation/summary.json)
records the signed artifacts. The [serial excerpts](../../plans/evidence/erlang-native-grant-cancellation/serial-excerpts.txt)
show lookup slot reuse and cooperative shutdown.
