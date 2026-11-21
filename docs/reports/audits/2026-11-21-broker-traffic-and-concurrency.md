# 2026-11-21: Broker traffic and initial concurrency measurements

## Results and scope

The signed Knytos EL0 broker passed its remote protocol smoke test (API versions,
metadata, produce, fetch and list offsets). An independent kafka-python client
then produced and consumed **302 records in 60 seconds**, offering five records
per second from two producers to `events` partition 1. It reported zero gaps,
zero errors and zero final lag. Signed node shutdown confirmed broker worker
joins, network-scope release and PSCI poweroff.

A separate fresh guest completed the following exploratory acknowledgement
sweep with zero measurement errors. Each row lasted approximately eight seconds;
producer connections were warmed before timing. Records were 40-byte values,
`acks=1`, `batch_size=0`, `linger_ms=0`, one outstanding request per producer,
no producer retries, and negotiated Kafka API versions. Throughput includes the
completion of outstanding requests at the phase boundary. Percentiles are
nearest-rank sample percentiles, not confidence intervals.

| Producers | Partitions | Acknowledged records | Records/s | Ack p50 ms | Ack p95 ms | Ack p99 ms |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 1 | 69 | 8.5 | 119.6 | 124.7 | 125.5 |
| 2 | 1 | 1338 | 167.0 | 11.2 | 12.9 | 61.8 |
| 4 | 1 | 1315 | 163.9 | 23.6 | 30.5 | 34.7 |
| 4 | 4 | 1347 | 167.9 | 22.5 | 29.7 | 76.3 |
| 1 (repeat) | 1 | 67 | 8.3 | 120.4 | 123.1 | 126.0 |

With an additional TCP connection holding an incomplete 128-byte frame, another
connection successfully produced a record in **25.8 ms**. This demonstrates
progress alongside an incomplete peer request in this run. It does not qualify
arbitrary stalled-handler fairness or latency bounds.

The single-producer result repeated after the concurrent phases. Increasing
from two to four producers did not increase throughput, and distributing four
producers across four partitions did not materially change the observed ceiling.
The large single-producer latency increase is a profiling lead. These results
alone do not identify whether TCP packet/ACK behaviour, service polling, CQ wake
paths, scheduling or another serialized stage produces that effect.

## What this says about the asynchronous implementation

The broker's CQ-driven socket state machines permit multiple sessions to make
progress; socket receives and deferred sends retain pending operation owners.
The partial-frame observation exercises that boundary. Partition state is owned
by two Sitas worker threads and reached through typed mailboxes.

Business dispatch is still synchronous: `Session::progress` calls
`Engine::handle_frame`, whose broker operations wait for a partition reply before
returning to the network loop. Thus asynchronous socket handling does not yet
make every business operation concurrent. The measured plateau is consistent
with a serialized stage but does not locate that stage. This experiment does
not compare against a synchronous control and cannot assign a speedup to async.
It exercises Sitas's Knytos reactor/thread/mailbox backend; it is not a general
benchmark of Sitas `ShardExecutor` future polling or Linux io_uring.

The Python client used about 0.32 CPU seconds per eight-second concurrent
phase. The QEMU process used about 8.75–9.03 CPU seconds per concurrent phase;
its three-second idle observation used 1.25 CPU seconds. These include emulator
and all guest services, not isolated kernel/runtime costs or proof of idle
quiescence. An unrelated QEMU guest was running at the start and was absent
at final inspection; its exit was not captured, so the period of competing host
load is unknown. No core pinning, host isolation, packet-time trace or
per-component CPU profile was used.

The next discriminating measurement is an end-to-end timestamp trace across
client send, TCP receive, IPC delivery, CQ wake, broker dispatch, shard reply and
TCP response, particularly with only one active producer. Then repeat longer
concurrency/partition sweeps with explicit batching and a separately controlled
native baseline. This run measures unbatched in-memory acknowledgements, not
persistent-write performance, replication, maximum bandwidth or native hardware.

## Environment and artifact identities

- Host: macOS 26.7.3 arm64; QEMU 11.1.2.
- Guest: AArch64 `virt`, Cortex-A710 under TCG, four vCPUs, 512 MiB, SLIRP host
  forwarding. The existing guest was initially PID 29510; the test guests used
  PIDs 37270 and 41321 and separate management ports. No evidence was captured
  identifying when or why the original guest exited.
- Kernel: dev/debug profile with opt-level 3 and LTO, native regression fixtures
  enabled; regular operational and test services, including heartbeat logging.
- Application: release EL0 broker, two partition workers, four `events`
  partitions, eight network-session limit, 64 KiB in-memory retention per partition.
- Rust: nightly-2026-11-14; kafka-python 2.3.2.
- OS base: `81096bc7844e513d1faf9e654faee4846ca8d966`, with the diagnostics-path
  correction described below. The per-run manifest recorded exactly three dirty
  paths: `scripts/boot-diagnostics.py`, `scripts/tests/test-boot-diagnostics.py`,
  `scripts/tests/test-boot-images.py`.
- Sitas: `0b2a85c144f178dd85992e42889464937a498f74`.
- Broker source: `4a9347b`; local `knytos.lock` adopted the above OS/Sitas SDK
  snapshot with manifest SHA-256
  `6d4d6317e39c13ff44fdd28402975d4e9d60364f05e762291514b4c7b8eb7719`.
- Signed broker ELF SHA-256:
  `90ca64378bdcdd680a3e45600641d7e38c8e86e03805db64ef6c2751f499a602`.
- Successful 60-second traffic guest kernel SHA-256:
  `e0218059902edb7b16b55c9f8f9f9605343ada54183c326cec341eb322663084`.
- Dedicated concurrency guest kernel SHA-256:
  `2065a28fb6f931d15c56e696a76ff3879632ed80bd795081656bb6c00317a38d`.
- Dedicated concurrency boot-image SHA-256:
  `3cd66521c4844098fa8b1d48b1be54fa50ee346631be48597876f6bf73faa276`.

Both successful guests reported `remote smoke ok: partition 0 base offset 0,
high watermark 1, latest 1` and `cooperative broker drain and node poweroff
verified`. The latter wrapper requires the actual worker-join marker and waits
for the signed node-shutdown fixture's verified poweroff result.

## Failed attempts and corrections

1. The first traffic fixture failed before QEMU launch: diagnostics attempted
   `mcopy -i ./os-images/knytos-aarch64-debug.img ::/catten .../kernel.elf` and
   received exit status 1. The shared image builder and Limine configuration
   package/load `/knytos`. Diagnostics and the two corresponding test assertions
   now use that current name. Fourteen diagnostics tests and three real mtools
   image tests passed; the later guest boots exercised the corrected extraction.
2. The initial acknowledgement harness forced `api_version=(0,11,0)` and failed
   metadata discovery after five seconds, with no phase samples. That bypassed
   this subset broker's supported-version negotiation. The configured legacy
   client profile and the broker's advertised subset differ; broker logs showed
   `session closed: InvalidReply`. The successful harness negotiates versions.
   The failed result is retained separately in `rejected-client-version.json`.
3. A corrected three-second sweep shared the traffic guest's remaining lifetime.
   Signed shutdown interrupted its four-producer phase. The resulting four
   acknowledgement timeouts are excluded from the completed sweep above and
   retained in `shutdown-interrupted.json`. The separate performance fixture
   provided a complete measurement window; separating these runs does not prove
   improved production latency or fairness.
4. A proposed cached-kernel repeat was rejected before boot with
   `error: management probes cannot provision a previously built kernel`.
   That guard remains intact. The dedicated successful run used normal kernel
   and service builds with fresh storage and management provisioning.

## Version-controlled evidence and reproduction

All evidence for the reported measurements is under
[evidence/2026-11-21-broker-concurrency](evidence/2026-11-21-broker-concurrency/):
[measurement client](evidence/2026-11-21-broker-concurrency/client.py),
[complete results](evidence/2026-11-21-broker-concurrency/results.json),
[traffic output](evidence/2026-11-21-broker-concurrency/traffic.txt),
[rejected client version](evidence/2026-11-21-broker-concurrency/rejected-client-version.json)
and [shutdown-interrupted sweep](evidence/2026-11-21-broker-concurrency/shutdown-interrupted.json).
The retained client uses fixed local ports and writes its JSON result to a
local temporary output; the committed JSON is the evidence for this report.
Its optional QEMU CPU sampling expects the PID file named in the script.

Run the broker's `tools/qemu-smoke.sh` with `KNYTOS_DIR` and
`KNYTOS_SITAS_DIR` naming explicit source inputs, management forwards 18080/18081,
application forward 19099, fresh fixture storage and a probe window of at least
85 seconds for the standalone sweep. Set `KNYTOS_QEMU_PID_FILE` to the PID-file
path expected by the retained client. Start the client immediately after the
remote smoke success marker, using the broker's existing kafka-python venv.
Keep the 60-second producer/consumer traffic check in a separate fixture/window;
it uses `tools/soak/soak_client.py --partition 1 --duration 60 --rate 5
--producers 2 --max-errors 0`. Preserve the signed drain/poweroff check after
traffic instead of interpreting a worker acknowledgement as completed exit.
