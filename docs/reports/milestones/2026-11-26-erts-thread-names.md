# ERTS thread names — 2026-11-26

The [thread adapter](../../../ports/erts/README.md#thread-names) now accepts
names up to fifteen bytes at creation and provides `ethr_getname` and
`ethr_setname`. Creation copies the name before native start. Each
participant can rename itself. Overlong creation returns `EINVAL`,
and overlong rename leaves the current name unchanged.

Fixed atomic storage holds name bytes, generation and a sequence counter.
Readers make one snapshot attempt and return `EAGAIN` on overlap.
Successful reads copy a complete null-terminated name. A short buffer
returns `ERANGE` and a retired identity returns `EINVAL`. All error
paths preserve the output buffer. Name retirement follows confirmed join
or rejected native start.

## Signed qualification

The two-CPU AArch64 QEMU campaign passed 27 signed cases and three
rejection controls. The name fixture created a thread with a fifteen-byte
name from mutable caller storage, changed that storage after create,
and verified that the worker retained the copied name.

The fixture checked short-buffer rejection, startup naming, overlong
rename behavior and concurrent snapshots during a bounded rename loop.
Every successful snapshot matched a complete published name.
The fixture allowed `EAGAIN` while preserving its output sentinel.
It did not require a particular number of overlapping snapshots.

An exit destructor looked up its own renamed thread. After join, the
same identity was rejected. A second generation on the same slot
received its new name while the old identity remained stale.
The existing thread fixture also rejected overlong creation before
submission. All name-fixture events were destroyed and the fixture
exited with zero remaining libc allocations.

The [run summary](../../plans/evidence/erts-thread-names/summary.json)
records source and signed artifact identities. The
[serial excerpts](../../plans/evidence/erts-thread-names/serial-excerpts.txt)
retain the fixture's completion and allocation result. Python syntax,
Rust formatting and diff checks passed.

## Remaining integration

Names are held by the native ERTS adapter and remain available through
its retained worker lifetime. Full ERTS still needs broader thread
capacity, background detached-thread reaping, stack and explicit-exit
contracts, thread-progress integration and backend lifecycle wiring.
Sequence-counter exhaustion was outside the executed fixture.

Repeat the qualification with a fresh output directory.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/name-validation
```
