# Native ERTS fixed console streams and terminal CPU preparation

Observed 2026-11-28 on `feature/erlang-runtime`, based on
`6521d20711405375a669b4254a210c88c90f511b` plus the source identities in the
[manifest](../../plans/evidence/erts-console-startup/summary.json).

Native ERTS default stream formatters now use the locked C runtime stdout/stderr.
CPU pre-early initialization rejects failed allocation and invalid startup
state through terminal native diagnostics before publishing its outputs/owner.
These are executed startup-prefix and formatter contracts, not a full ERTS
startup or emulator boot.

## Reproduce

Configure with the same pinned OTP 29.1.2 and Picolibc 1.8.12 sources as the
preceding steps, plus native-console mode. Source-preservation checks require
the host `unifdef` tool.

```sh
python3 ports/erts/configure.py --native-ethread --native-clock --native-platform \
  --native-metadata --native-time --native-pre-init --native-internal \
  --native-console --clock-sample-age-ms 5000 --otp-source /path/to/otp29 \
  --picolibc-source /path/to/picolibc --output /fresh/native-console
python3 ports/libc/run-tests.py --erts-time-init \
  --erts-platform-build /fresh/native-console --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --case 14 --case 15 --case 16 --case 17 --case 18 --case 37 \
  --case 92 --case 93 --case 94 --case 95 --case 96 --case 97 \
  --case 98 --case 99 --case 100 --case 101 --case 102 --case 103 \
  --output /fresh/native-console-core
python3 ports/libc/run-tests.py --erts-time-init \
  --erts-platform-build /fresh/native-console --smp 2 \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --case 104 --case 105 --case 106 --case 107 --case 108 \
  --output /fresh/native-console-focused
```

OTP is `d199a49a01a2079afb6f2c5c9ae047cd4555e68d`; Picolibc is
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. Use fresh paths without whitespace.
The split retains mandatory base C controls and a final survivor in each guest.
It covers the combined fixture union while preserving the failed larger run
below. No wait deadline or production persistence policy is relaxed.

## Stream and startup contracts

The native helpers in common `erl_printf.c` use actual `fputc`/`fwrite` on
the fixed stdout/stderr objects. Default printf/fprintf and va_list variants
hold the existing recursive stream lock through a complete formatted call;
native `erts_vprintf` now acquires that whole-call lock too. Configured callback
overrides retain their upstream behavior and are outside this qualification.
Unsupported FILE objects reject before locking, and callback spans beyond
`INT_MAX` reject before I/O. CRLF conversion reports the original logical
length. A short output or character failure returns a negative error after
any already-written prefix; there is no replay or fabricated success.

The CPU pre-early function checks startup identity before reading its mutable
runtime owner, rejects an existing owner, and prepares the new CPU-info object
before writing caller outputs or publishing that owner. Failed allocation
diagnoses and aborts before later initialization can dereference a null owner.
The diagnostic result is ignored only because abort remains mandatory even
when logging fails. Successful runtime ownership and normal post-worker C
exit remain unchanged. No kernel cleanup contract or owner family is added;
the existing C16 qualification row records the stream and terminal boundaries.

The source patch changes only native branches. Removing `ERTS_KNYTOS` with
`unifdef` produces the original source lines after ignoring indentation and
empty lines; string-literal whitespace is preserved in that comparison.
The manifest records base Git blobs and patched native identities for the
formatter/CPU sources. The other two internal C sources still match the pin.
Both three-member internal archives build with real generated Make rules.
Positive consumers pass native ELF checks with only the SDK's existing optional
weak entropy hook. Descriptor output and `select`-based sleep still fail linkage.
Mode/profile rejection controls, pinned YCF tests, generated metadata, common
source compilation and eight host calendar cases also pass.

## Signed fixture behavior

Final fresh-guest campaigns passed 29 core cases and 16 focused cases, each
with three deployment controls. Their union covers all 34 distinct fixtures,
including diagnostic failure and a final survivor in each guest. Results are
retained separately for the [core](../../plans/evidence/erts-console-startup/core/guest-result.json)
and [focused](../../plans/evidence/erts-console-startup/focused/guest-result.json)
cohorts. This is split qualification, not a passing combined campaign.

Both internal-library variants execute actual stream formatting and varargs
forms on startup and a real worker. Fixtures keep stdout/stderr recursively
locked while the worker publishes its start, then release before exact join.
The harness requires eight complete MAIN and eight complete WORKER records,
without mixed bodies. A long record exercises bulk output. Test-only link
wrappers inject a character error and a real short-write prefix, and count
each fault once. Another capture checks exact CRLF bytes while still delegating
successful output to Picolibc. Invalid FILE and oversize-span checks reject.
Normal C exit releases the one retained CPU-info block after worker/event drain,
with zero remaining C allocations.

Separate domains test committed-state reentry, worker invocation, allocation
failure and failed diagnostic output. An abort wrapper observes unchanged
caller sentinels and zero prepared C blocks before the actual domain abort.
The dropped-diagnostic case must still reach that observer and terminal abort.
These test-link wrappers are absent from production archives. Later survivors
check continued node operation; terminal markers do not prove physical root
retirement or authorize early owner release.

## Failed combined campaign

The initial 34-case campaign passed 32 expectations, including both stream
variants and CPU reentry/worker controls. Admission of the diagnostic-failure
fixture then returned HTTP 503 with error -22. Raft-store logged a fatal
persistence write rejection: object `0x875880ae12092ea9`, 9,849 bytes and
`RemoteStatus(2)`. The object-store protocol identifies status 2 as
`ERR_NO_SPACE`. That fixture and the final survivor were not executed.

The retained [bitmap analysis](../../plans/evidence/erts-console-startup/failed-combined/bitmap-analysis.json)
records the disk identity, superblocks, bitmap, all free runs and relevant source
identities. It contains 10,195 free 512-byte blocks. After the implementation's
one-block header reservation, the first sixteen free runs provide nineteen data
blocks; this log requires twenty. The increasing-order allocator stops at its
sixteen-extent limit before a later large run. This is consistent with the
[earlier extent-selection finding](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign).
It is not a general capacity bound or proof that every persistence failure has
that cause. The live allocator/RPC state was not captured. The full disk and
binaries are not versioned; the raw bitmap/superblocks, diagnostics, source
identities and [failed guest result](../../plans/evidence/erts-console-startup/failed-combined/guest-result.json)
are retained. Persistence recovery remains unqualified.

Final campaigns use fresh guests and selected fixture cohorts. Their success
does not close the failed combined run or the storage incident. The failed
harness snapshot is preserved separately; the final harness adds case selection
and complete-record validation. Runtime archives and versioned C fixture
templates are unchanged between those runs; generated source paths and image
identities are recorded per cohort.

| Artifact | SHA-256 |
| --- | --- |
| Regular/reentrant internal archives, each | `6e1af929cd5110cb009662542c5b656f56e25cc2043539a7eca5faa9ae4cf0b4` |
| Native common CPU archive | `f13be76e085511b8a8533b07d9939986ed84835f1538bba89acdab3d9d4fcac9` |
| Core guest kernel | `b3592e7f5ba795c077f337f13107e2af3c5efac0a889f167c77aa5ec13f75241` |
| Focused guest kernel | `a465fe1a05d6255836df745a19168a73be280616eeb684aabeca79175906350d` |

The manifest retains each cohort's firmware, boot-image, initial-disk and
fixture identities; no kernel/image byte equivalence between guests is
claimed. [Checksums](../../plans/evidence/erts-console-startup/checksums.json)
cover retained evidence. Final source identities, Python syntax and staged
whitespace checks pass. No Rust source changed.

## Remaining work

CPU discovery/binding, later topology/allocator initialization, descriptor I/O,
sleep, real stack boundaries, adequate thread capacity, continuous clock refresh,
later platform hooks, timer/correction execution, full dependency closure and
matching host bootstrap/preloaded modules remain open. No complete emulator
startup, scheduler execution or Erlang BIF execution is claimed.
