# AtomVM deployment retirement — 2026-11-24

The [fixed-scheduler profile](2026-11-24-atomvm-native-schedulers.md)
was pushed as `49811ef0ff3c84a701e2094451f99dc902b77428`. This increment
exercises signed deployment replacement while the original VM is alive.
The controller accepts one descriptor for each generation. The agent
retains its kernel-returned launch owner through old-domain retirement
before launching the successor.

## Baseline

The original four-scheduler busy image launched all three native workers
and reached its workload marker. After the signed replacement, a
condition wait reported shutdown. Two other participants logged libc
lock acquisition failures. The old domain aborted and the successor
reached its workload marker. The run failed the new assertion for an
explicit shutdown path. Its serial excerpt retains both generations
and the concurrent terminal diagnostics.

Under runnable work, AtomVM calls the platform hook with its nonblocking
argument even when it never enters a CQ wait. The original hook returned
immediately in that case. The observed baseline retired through a private
wait. That run does not establish how long a VM would continue without
such a wait. See the
[baseline capture](../../plans/evidence/erlang-deployment-retirement/baseline-serial.txt).

## Runtime change

The nonblocking scheduler checkpoint checks the authenticated lifecycle
page after the ready-list and scheduler guards leave. This marks the
request seen while leaving CQ readiness and retirement ownership intact.
Idle schedulers continue to observe shutdown through their checked CQ
wait, and a private synchronization wait can observe the request first
under load. The shutdown paths emit one record per observing participant
through the Rust console. The domain abort leaves terminal quiescence
and reclamation with the existing kernel retirement owner.

The [signed runner](../../../ports/atomvm/README.md#signed-deployment-retirement)
uses a four-CPU guest and signs generation two against the same image
and thread limit as generation one. A five-second grace period applies
to each descriptor. The workload requires a live VM before submission
and checks the shutdown diagnostic before the old-domain abort.
Generation two must reach its own workload marker. The evidence allows
ASID reuse because the launch generation and ordered retirement marker
identify the predecessor and successor.

## Results

All 28 signed cases and three rejection controls passed on QEMU 11.1.3.
Both busy configurations observed the request at the nonblocking
checkpoint. Two additional workers in the four-scheduler case reported
shutdown from private waits. The parked case observed the request on CQ
zero while all three native workers stayed outside the interpreter.

| Fixture | Native schedulers | First shutdown observation | Old and new ASID |
| --- | ---: | --- | ---: |
| Parked workers | 4 | CQ zero wait on LP 0 | 43 |
| Continuously runnable work | 1 | Nonblocking checkpoint on LP 0 | 44 |
| Continuously runnable work | 4 | Nonblocking checkpoint on LP 2 | 45 |

Each original domain reached the kernel abort path, and each successor
reached its workload marker with the same numeric ASID. The final serial
excerpt records the exact order. The 89 Rust host tests, 24 boot and
deployment script checks, target Clippy for both C profiles and workspace
formatting passed. See the
[artifact summary](../../plans/evidence/erlang-deployment-retirement/summary.json)
and [qualified serial capture](../../plans/evidence/erlang-deployment-retirement/qualified-serial.txt).

## Remaining work

Deployment retirement uses domain abort. Cooperative VM disposal,
whole-node shutdown with AtomVM present, waiter-admission exhaustion and
performance qualification remain open. The
[multicore plan](../../plans/erlang-m3-multicore.md) tracks those gates.
