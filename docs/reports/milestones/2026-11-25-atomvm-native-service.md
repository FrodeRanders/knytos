# AtomVM scoped native service calls — 2026-11-25

The initial M4 profile lets an Erlang process submit an asynchronous
scalar request through a signed Knytos service grant. It uses one
Erlang scheduler and a dedicated worker on guest LP 1. A fixed slot
bounds outstanding work. The VM sees an Erlang reference and either
a scalar result or an error atom.

The worker reuses the Rust application grant client and IPC owners.
It borrows the controller and descriptor from launch, acquires the
requested call grant and retains the native connection through
terminal observation or cancellation. Any returned attachment is
disposed on the worker before it publishes the scalar outcome. The
worker signals CQ zero and the scheduler delivers to the requesting
process if it is still live. Startup joins the worker before terminal
C cleanup and signed lifecycle acknowledgement.

The native budget begins when the worker takes the slot and spans both
grant acquisition and call observation. The worker holds native IPC
owners independently of NIF calls and process finalizers. The first
profile covers one outstanding scalar request with single-scheduler AtomVM.
See the
[build and API contract](../../../ports/atomvm/README.md#scoped-native-service-calls).

## Signed validation

The signed AArch64 QEMU run passed 13 cases and three rejection
controls on two guest CPUs. The native service fixture observed the
time service's deterministic scalar error reply. It checked malformed
opcode rejection, the fixed slot's busy reply, denial of an unlisted
RNG grant and rejection of a returned observability memory attachment.
An Erlang timer fired while an accepted request for a missing service
still occupied the native slot. A later request succeeded after the
original Erlang recipient exited and its worker operation settled.

The service shutdown fixture kept Erlang work runnable while its
helper submitted requests. It observed the authenticated shutdown
at the nonblocking scheduling checkpoint, joined the service worker
and reported zero tracked C blocks. The supervisor acknowledged
generation 1 after reclamation. QMP reported `guest-shutdown`
and QEMU exited with status zero. Eight C ABI and application SDK
host tests passed.

The [run summary](../../plans/evidence/erlang-native-service/summary.json)
records the signed grants, image hashes and shutdown result. The
[serial excerpts](../../plans/evidence/erlang-native-service/serial-excerpts.txt)
retain the service, timer and teardown markers.
