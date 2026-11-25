# AtomVM native request cancellation — 2026-11-25

`knytos:cancel/1` lets the submitting Erlang process withdraw interest
in an [asynchronous scalar request](2026-11-25-atomvm-native-service.md).
The scheduler checks both the Erlang reference and the submitting local
process. A successful cancellation suppresses delivery. A foreign,
stale or already delivered reference returns `{error, not_found}`.

An atomic transition decides whether the worker claims a queued request
or the scheduler releases the slot. Claimed work retains the slot
through worker cleanup and result observation. The worker can skip
grant acquisition or service invocation when cancellation is visible at
those checkpoints. An accepted native call stays with its original
owner and deadline and can have remote effects. Earlier mailbox
delivery stays with the recipient.

## Validation

The signed two-CPU AArch64 QEMU run passed 13 cases and three rejection
controls. The fixture rejected cancellation from another Erlang process
holding the reference and treated a reference whose reply was delivered
as stale. After cancelling the slow request, a replacement received
`busy` while the slot was still occupied. An immediate cancellation in
the same run allowed a replacement to submit at once. Neither cancelled
reference delivered a reply, and the slot served subsequent requests.

The signed shutdown fixture reached the nonblocking checkpoint, joined
the worker and reported zero tracked C blocks before generation 1 was
acknowledged. QMP reported `guest-shutdown` and QEMU exited with status
zero. Eight application SDK and C ABI host tests also passed.

The [run summary](../../plans/evidence/erlang-native-cancellation/summary.json)
contains the signed artifact identities and cancellation markers.
The [serial excerpts](../../plans/evidence/erlang-native-cancellation/serial-excerpts.txt)
retain their ordering and the cooperative shutdown result.
