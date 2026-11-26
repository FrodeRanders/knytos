# AtomVM native mailbox backpressure — 2026-11-26

The single-scheduler service profile now admits eight native deliveries
across the VM. The request slot reserves a reply before worker submission.
A close subscription also reserves its terminal event. Insufficient
capacity returns `{error, busy}`. The scheduler tracks the exact queued
message until selective receive or mailbox destruction. Native worker
settlement remains governed by the existing call and watch owners.

## Validation

The signed fixture filled one process's mailbox with eight native replies.
Another process received `busy` when it attempted a call or lookup.
Receiving an Erlang-sent reply-shaped tuple left the backlog full. Selective
receive of a native reply admitted a new call. Terminating the holder
with seven unread replies restored all eight slots.

The fixture also rejected watch registration with room for only one
delivery. With an admitted watch and seven queued replies, signed service
replacement delivered the terminal event. The client received that event
with the seven replies still queued, then drained them and reacquired
the replacement service.

The two-CPU AArch64 QEMU 11.1.3 campaign passed 14 signed cases and
three rejection controls. The fixture joined the service worker and exited
with zero remaining libc allocations. Signed node shutdown produced the QMP `guest-shutdown`
event and QEMU exit status zero.

The [run summary](../../plans/evidence/erlang-native-backpressure/summary.json)
identifies the tested builds and signed artifacts. The
[serial excerpts](../../plans/evidence/erlang-native-backpressure/serial-excerpts.txt)
record backlog recovery, watch delivery, and shutdown.
