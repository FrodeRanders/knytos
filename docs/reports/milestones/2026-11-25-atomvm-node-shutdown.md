# AtomVM whole-node shutdown — 2026-11-25

One signed node shutdown drained two live four-scheduler AtomVM
deployments on the same four-CPU AArch64 guest. One VM had all three
workers running Erlang work. The other kept all three workers parked
outside the interpreter. Both used the heap-only profile from
[cooperative deployment retirement](2026-11-24-atomvm-cooperative-retirement.md)
at `9824e613`.

The native runner waited for both workload markers and submitted one
signed shutdown intent through the mTLS management ingress. Four held
connections closed during the deployment and HTTP ingress drain. The
intent used a 60-second node grace period and five-second phase grace
period. Each VM also had a five-second signed retirement grace period.

| Deployment | Original ASID | Shutdown observation | Native workers joined | C blocks released |
| --- | ---: | --- | ---: | ---: |
| Parked workers | 40 | CQ zero wait | 3 | 44 |
| Continuously runnable work | 41 | Nonblocking scheduler checkpoint | 3 | 76 |

Both VMs reported zero remaining tracked blocks. The supervisor logged
both exact root-generation acknowledgements after reclamation. The
deployment agent completed its application drain and retired
cooperatively. The kernel reached PSCI system-off, QMP reported
`guest=true` with reason `guest-shutdown`, and QEMU 11.1.3 exited with
status zero.

The kernel change adds the shutdown reason to its existing
acknowledgement diagnostic, using the captured root after successful
reclamation. The agent and coordinator use their existing owners and
deadlines. The [native runner](../../../ports/atomvm/README.md#signed-whole-node-shutdown)
checks joins and allocation cleanup separately for each VM before
checking the agent and poweroff.

The combined run passed 24 native cases and three rejection controls.
A separate deployment-replacement run passed 13 native cases and three
rejection controls. Both VM fixtures reached the workload marker in
their successor generation after the original root was acknowledged.
The management drain, boot diagnostics, boot result and CI-input
scripts passed 32 checks. See the
[artifact and result summary](../../plans/evidence/erlang-node-shutdown/summary.json)
and [serial extract](../../plans/evidence/erlang-node-shutdown/node-serial.txt).
