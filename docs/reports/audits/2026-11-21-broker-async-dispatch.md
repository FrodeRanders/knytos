# 2026-11-21: Broker business futures and TCP progress scheduling

## Results

The EL0 broker now retains asynchronous business continuations. A session awaiting
partition or coordinator work yields to other sessions on the same network loop.
A deterministic gated-worker regression confirms that metadata and a produce on
another shard finish while the first produce and a coordinator request remain
pending. It drives the actual session dispatch, real Sitas worker tasks and the
independent `knytos-kafka` client codec. It also verifies that dispatch polling
never parks, an unpolled dispatch never submits, worker shutdown rejects active
request scopes before starting teardown, and shutdown joins after those scopes
settle.

The clearest measured performance improvement came from a separate TCP/IP service
fix: repeat the service loop after actual local progress before admitting an idle
wait. With the original, byte-identical broker ELF, single-producer acknowledgement
median fell from about **120 ms to 6.2 ms**, and throughput increased from about
**8.5 to 158 records/s**. Four-producer throughput was around **194–203 records/s**.
The later broker with business futures and owned-buffer/deque improvements achieved
**152–155 records/s** with one producer and **201–202 records/s** with four.
This short workload shows no clear additional throughput gain from the broker
changes. Its useful async result is independently demonstrated concurrency during
a delayed business operation, rather than a claimed speedup for tiny memory-only
commands.

Each updated guest passed API versions, metadata, produce, fetch and list-offset
smoke checks; produced and consumed **302 records in 60 seconds**, with **zero gaps
or errors**; and completed signed shutdown with actual worker joins, network-scope
release and PSCI poweroff. Both concurrency sweeps completed without measurement
errors. Their shutdown receipts are retained alongside their traffic outputs.

## Comparable acknowledgement sweeps

The measurement client and settings are those of the
[initial experiment](2026-11-21-broker-traffic-and-concurrency.md): five eight-second
phases; 40-byte values; `acks=1`; no batching, linger or producer retries; one
outstanding request per producer; negotiated API versions; warmed producer
connections. The two new sweeps followed the 60-second partition-1 traffic test
inside their respective fresh guests. The initial sweep used its own fresh guest.
Percentiles are sample percentiles, not confidence intervals.

| Producers / partitions | Initial records/s | Initial p50 ms | TCP progress records/s | TCP progress p50 ms | + business futures / owned buffers records/s | + business futures p50 ms |
| --- | --- | --- | --- | --- | --- | --- |
| 1 / 1 | 8.5 | 119.64 | 158.1 | 6.23 | 155.3 | 6.34 |
| 2 / 1 | 167.0 | 11.19 | 182.6 | 10.76 | 171.3 | 11.11 |
| 4 / 1 | 163.9 | 23.61 | 194.5 | 20.06 | 201.3 | 19.30 |
| 4 / 4 | 167.9 | 22.46 | 202.7 | 19.30 | 202.3 | 19.34 |
| 1 / 1 (repeat) | 8.3 | 120.42 | 158.7 | 6.21 | 152.3 | 6.42 |

An incomplete-frame connection coexisted with an acknowledged produce on another
connection: 25.8 ms initially, 30.0 ms with TCP progress and 44.2 ms with business
futures. Those isolated samples establish progress, not a latency bound. They do
not exercise a stalled business handler; the gated-worker regression does.

The same TCP/IP ELF was used in both new guests. The TCP-only application ELF
matches the initial experiment exactly, separating its service change from the
later broker implementation. Nevertheless these are single short runs under
TCG/SLIRP, without host CPU isolation or packet/component tracing. Host load and
per-run trust provisioning were not controlled. The initial experiment also had
an unrelated guest at an incompletely observed point in its lifetime. These
limitations prevent a general async speedup claim or a hardware performance claim.

QEMU used roughly 9.0–9.4 CPU seconds per eight-second updated phase, including
all guest services and emulation. Three-second idle observations used 1.30 and
1.65 CPU seconds respectively. These are not isolated kernel/runtime CPU costs,
and the idle difference is not attributed to the implementation from one sample.
The remaining approximately 6 ms single-producer round trip and 200 records/s
concurrent ceiling need timestamp tracing across TCP, IPC, CQ wake and shard
reply. Large batches/payloads, real storage and replication need separate
experiments. TCP_NODELAY and precise smoltcp poll deadlines were not changed.

## Implementation and ownership boundaries

- TCP/IP repeats after accepted IPC, socket completion or reclamation. A blocked
  owner alone is not progress. Work per IPC turn and CQ draining remain bounded.
  The existing 50 ms idle maintenance limit remains for protocol timers and
  owner-status reclamation; the whole stack is not claimed to be event-driven.
- Business methods await the existing no_std Sitas `ShardReceiver::recv` future.
  Reply registration/wake uses the original network CQ/parker; wakes never adopt
  results or resubmit commands. The native network thread does not use the host
  blocking compatibility driver.
- A session owns decoded input, its future and response under its original
  request deadline. Multi-partition requests visit owners sequentially, yielding
  on each pending reply. Session count and one in-flight request per session
  remain bounded; no extra request threads or shared mutable partition logs were
  introduced.
- A full command mailbox rejects before acceptance. The pinned no_std sender
  lacks a producer-capacity readiness future, so this implementation does not
  disguise space polling as asynchronous backpressure. A later failure after
  transaction-coordinator completion fences the broker and retains the full
  resolution receipt; durable recovery is still absent.
- Accepted business work must settle. Expiry, abandoned started dispatches and
  exhausted host observation budgets are terminal with complete futures retained.
  Shutdown stops admission, drains accepted continuations under its authenticated
  absolute deadline, then closes network scopes and joins workers. A wake or
  acknowledgement alone does not prove thread exit.
- Appends consume owned key/value buffers without cloning their payloads. A
  `VecDeque` removes old batches without shifting survivors. Offset exhaustion
  rejects the whole append before mutation. Pointer-identity, transactional
  visibility, offset boundary and wrapped-retention fetch tests cover the change.

Current contracts are in the OS [smoltcp adapter](../../reference/smoltcp-adapter.md)
and broker `docs/architecture.md` and `docs/development-model.md`. OS implementation
commit: `18c31aea`; broker implementation commit: `0b11249`. Sitas source code did
not require changes: its pinned channel future already provides reply readiness.

## Qualification and artifact identity

Host validation passed 252 OS service-library tests and 68 broker workspace tests,
plus three EL0 host tests. Broker formatting, default-member all-target Clippy,
EL0 test-target Clippy and documentation passed. Native bundled services and the
AArch64 release broker built; both native guest runs completed their smoke, traffic,
concurrency and signed shutdown checks.

The first OS and broker host attempts inside the sandbox could not bind test
sockets; unrestricted repeats passed. An overlapping earlier Cargo/doc build had
transient missing-rmeta diagnostics; a sequential complete repeat passed. A host
EL0 `--all-targets` lint invocation rejected unsupported no_std unwind settings;
EL0 host linting used `--tests`, and the native build used the SDK target flow.
Two collapsible-if lint findings were corrected. These are separate from native
measurement results; no failed native attempt was replaced by a passing claim in
this follow-up. The initial report retains its own failed attempts unchanged.

Environment: macOS 26.7.3 arm64, QEMU 11.1.2, AArch64 Cortex-A710 TCG, four vCPUs,
512 MiB, SLIRP; kernel dev/debug opt-level 3 with LTO and regression fixtures;
release application, two workers, four `events` partitions, eight session slots
and 64 KiB retention per partition; nightly-2026-11-14; kafka-python 2.3.2.

Both runs used OS base `5c99fc8c96305827e8ee3d2f4b00b1e0f4eaf346` plus the
TCP progress change subsequently committed as `18c31aea`; Sitas was
`0b2a85c144f178dd85992e42889464937a498f74`. Local SDK manifest SHA-256 was
`936892783e782d2b28dff23b8bd3897b26e46c4dc0b105558ba5736b0514532f`.
Per-run manifests retain dirty paths, source-diff fingerprints, exact kernel and
boot-image identities and QEMU arguments. The second manifest additionally lists
untracked copies of the first run's evidence. Those copies do not change code.

| Artifact | TCP progress | + business futures / owned buffers |
| --- | --- | --- |
| Signed broker ELF | `90ca64378bdcdd680a3e45600641d7e38c8e86e03805db64ef6c2751f499a602` | `9c222eba97bc4fdf77cc9fabe008eb2807b391886644965bfa676ef8d49e51d0` |
| Kernel | `23626028b82cfcac27b9893a4f3179b4cd53ac36b0491ee823b25ecb30bd7287` | `babbc566e0a861ce8787ed67c4bc17926b411d8a880c3331f8d36af237d4b174` |
| TCP/IP ELF (both) | `1bab404414b5440e2d864d4ddace4b43a580c08d601654cabd8051c28ccd44bc` | same |

The measured broker source hashes are retained in `measured-broker-sources.json`,
based on `52ac678` plus implementation changes. Before commit, only comments,
additional test assertions and equivalent Clippy let-chain rewrites changed
relative to that measured snapshot. The final build/sign check produced
`d71b37ae0addb9654e05360f1a14362532fca43e9406964195e8c1359b8a6711`;
that rebuild is not presented as a separately executed native measurement.

## Evidence and reproduction

[Retained evidence](evidence/2026-11-21-broker-async-dispatch/) contains per-case
`results.json`, `traffic.txt`, fixture output, shutdown markers and `manifest.json`,
plus the measured application source fingerprints. The
[original measurement client](evidence/2026-11-21-broker-concurrency/client.py)
remains unchanged. Full percentile/error/CPU samples are in each result file.

Run the application `tools/qemu-smoke.sh` with explicit OS and Sitas source paths,
HTTP/deployment/application host ports 18080/18081/19099, a 155-second probe hold,
and a 480-second runner timeout. Start `tools/soak/soak_client.py --partition 1
--duration 60 --rate 5 --producers 2 --max-errors 0` immediately after the remote
smoke marker, then run the retained concurrency client in the remaining window.
Keep signed shutdown enabled and require the worker-join and verified-poweroff
markers. Use normal kernel rebuilds; do not bypass the cached-kernel management
provisioning guard. Local source paths are explicit inputs, with no sibling-layout
assumption.
