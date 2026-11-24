# AtomVM cooperative deployment retirement — 2026-11-24

The earlier [deployment retirement qualification](2026-11-24-atomvm-deployment-retirement.md)
was pushed as `7dc2e5d4`. It exercised domain abort after signed
replacement. This increment joins the VM's native schedulers and
acknowledges retirement after local disposal. The agent retains its
kernel-returned launch owner through reclamation in both cases.

## Shutdown path

Startup assigns wait-time lifecycle delivery to the elected reactor
before submitting workers. The reactor observes a pending request at
a CQ wait or its nonblocking scheduler checkpoint. A pinned scheduler
adaptation rechecks stop after the platform hook before handing a
selected process back to the interpreter. It closes new scheduler
reservations once stop is visible. Private worker and synchronization
waits retain their existing peer producers and lock budgets.

The adapter keeps the entire VM graph alive until the exact native
joins complete. The C runtime validates the pending request and the
joined roster, then runs finalizers and releases tracked blocks.
It acknowledges through the existing Rust lifecycle API and exits
startup. The supervisor reports the acknowledged retirement only
after kernel reclamation completes.

The embedded interpreter in this profile has local C state and the
static console bridge. External NIF resource cleanup and Erlang
process exit callbacks are outside this retirement contract. Normal
VM return continues through its ordinary result and teardown hooks.

## Native results

All 28 signed cases and three rejection controls passed on QEMU 11.1.3
with four guest CPUs. The harness checked the original generation's join
and cleanup records before the supervisor acknowledgement and the
replacement's workload marker.

| Case | Shutdown observation | Native workers joined | C blocks released at exit |
| --- | --- | ---: | ---: |
| Four schedulers with parked workers | CQ zero wait on LP 0 | 3 | 44 |
| One busy scheduler | Nonblocking checkpoint on LP 0 | 0 | 63 |
| Four busy schedulers | Nonblocking checkpoint on LP 3 | 3 | 76 |

Each VM reported zero remaining allocations. The kernel acknowledged
each original root and the agent launched the subsequent deployment
generation. The C control that called completion without a pending
request aborted before running its finalizers. See the
[serial extracts](../../plans/evidence/erlang-cooperative-retirement/serial-excerpts.txt)
and [artifact summary](../../plans/evidence/erlang-cooperative-retirement/summary.json).

The initial attempt exceeded the fixed 16 MiB staging disk before
boot. The passing repeat omitted the archived single-scheduler hello
image and retained the new 1-, 2-, and 4-scheduler message and timer
workload and all three retirement cases.

All 19 AArch64 debug kernel self-tests, 89 Rust host tests and 15 boot
and CI-input checks passed. Target Clippy passed with warnings denied
for both libc profiles, and workspace formatting passed. See the
[kernel test records](../../plans/evidence/erlang-cooperative-retirement/kernel-selftests.txt).

Whole-node shutdown, wait admission under pressure and performance
remain open. See the [retirement fixture](../../../ports/atomvm/README.md#signed-deployment-retirement).
