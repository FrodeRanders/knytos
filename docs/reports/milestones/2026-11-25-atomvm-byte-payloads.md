# AtomVM bounded binary service payloads — 2026-11-25

`knytos:request/4` accepts a binary of up to 4096 bytes and works with
signed service names and reusable handles. The worker prepares an owned
page and moves it through IPC. An empty binary still supplies an attachment.
The scalar argument stays available to the service protocol.

A memory reply uses the scalar result as its initialized byte count.
The worker validates the count and backing size, copies the prefix, and
releases the native owners before the scheduler publishes a binary.
A scalar reply keeps the existing integer result. Three-argument
requests continue to reject attachments.

## Validation

The signed two-CPU AArch64 QEMU campaign passed 14 cases and three
rejection controls. The native fixture echoed empty, short, and full-page
binaries through named and reusable service calls. The client rejected
an oversized input, negative and oversized reply lengths, and a two-page
reply object whose declared prefix was one byte. A subsequent echo
succeeded. The fixture also exercised signed grant denial and a scalar
reply to a request that moved a page.

The service recorded receipt of a moved attachment before the explicit
cancellation trial and before the timeout trial. Both late replies were
rejected. The regression fixtures verified process-exit cleanup, service
handle release, and signed node shutdown. QMP reported `guest-shutdown`
and QEMU exited with status zero. The host suite passed 75 unit tests and
one compile-fail doctest.

The [run summary](../../plans/evidence/erlang-native-byte-payloads/summary.json)
records the signed artifacts and the [serial excerpts](../../plans/evidence/erlang-native-byte-payloads/serial-excerpts.txt)
show the ordered service and client observations.
