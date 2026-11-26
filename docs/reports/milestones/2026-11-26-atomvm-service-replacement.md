# AtomVM service replacement — 2026-11-26

The signed fixture replaces the publisher of `m4.hold` while the same
Erlang VM keeps its first handle and waits on its endpoint-close subscription.
Both deployments use the same signed ELF and the same publish grant. The
fixture reports the sequence from its launch descriptor so the client can
verify which deployment handled each request.

## Validation

The signed two-CPU AArch64 QEMU campaign passed 14 cases and three
rejection controls. The first publisher reported sequence 1. The VM
received its close event during cooperative retirement and rejected
requests with the retired handle. The supervisor acknowledged retirement
of root generation 1 before launching the replacement. Both publishers
used ASID 29.

The client looked up the same name again, received sequence 2 and completed
a binary round trip. It retained the old handle and subscription throughout
the replacement and verified that neither could act on the successor. It
then observed a separate close event when the successor closed its own
endpoint while the VM waited without an Erlang timeout.

The campaign also verified signed node shutdown. QMP reported
`guest-shutdown` and QEMU exited with status zero. The host suite passed
75 unit tests and one compile-fail doctest.

The [run summary](../../plans/evidence/erlang-native-service-replacement/summary.json)
records the signed descriptors and the [serial excerpts](../../plans/evidence/erlang-native-service-replacement/serial-excerpts.txt)
show retirement, launch, reacquisition and direct endpoint closure in order.
