# ERTS read/write mutex adapter — 2026-11-26

The [read/write mutex adapter](../../../ports/erts/README.md#readwrite-mutex-adapter)
implements the eleven OTP 29.1.2 entry points over native locks. Shared
and exclusive blocking acquisition use new event-only native calls, with
the same guarded readiness, writer preference and waiter withdrawal as
timed acquisition. A wake remains a hint to recheck ownership.

The adapter adds exclusive try-lock and reports requested native heap
storage beyond the embedded ERTS wrapper. Reader grouping is an unused
hint, as in OTP's pthread backend. Valid type, lifetime and spin options
select the same registered native algorithm. Readers may share and nest;
exclusive acquisition cannot nest or upgrade an existing read lock.
The adapter maps held or incompatible try-lock ownership to `EBUSY`.

Busy destruction preserves the object. Successful destruction clears its
pointer. Callers retain the lock through acquisition and exclude concurrent
entry during destruction. Unexpected blocking or unlock errors abort the
domain after native waiter-interest withdrawal. Unlock and terminal
shutdown progress remain the enclosing backend's responsibility.

## Signed qualification

The two-CPU AArch64 QEMU synchronization campaign passed 23 signed cases
and three rejection controls. Its positive read/write fixture checked
shared readers, nested read depth and an exclusive waiter remaining
blocked until the last read release. The writer published protected
payload, then a subsequent exclusive owner released a blocked reader
that observed its updated payload. Both blocking access modes used the
event-only native path.

The fixture checked held and incompatible try-lock rejection, busy
destruction under shared and exclusive ownership, default and explicit
options, invalid options, successful disposal and double-destroy
rejection. The reported size matched the allocation-statistics delta for
one native lock. All three worker callbacks were joined before disposing
the shared input and synchronization objects, with zero remaining libc
allocations.

The negative fixture attempted a read release from a participant that did
not own the lock and emitted the expected adapter error and C abort. A
later signed C application completed successfully. Earlier mutex and
condition fixtures passed in the same campaign.

The separate worker-libc regression passed 32 signed cases and three
rejection controls. It retained the native timed read/write checks for
nested readers, writer-timeout withdrawal reopening reader admission,
and last-reader writer release. Host validation passed 77 unit tests and
one compile-fail documentation test, with two documentation tests ignored.
Rust formatting, Python syntax and diff checks passed.

The [focused summary](../../plans/evidence/erts-rwmutex-adapter/focused/summary.json)
and [regression summary](../../plans/evidence/erts-rwmutex-adapter/regression/summary.json)
record executable identities, checked native build inputs, adapter source
hashes and pinned upstream sources. Serial excerpts retain the
[adapter results and fatal controls](../../plans/evidence/erts-rwmutex-adapter/focused/serial-excerpts.txt)
and [timed-lock and earlier ERTS results](../../plans/evidence/erts-rwmutex-adapter/regression/serial-excerpts.txt).

## Remaining integration

The adapter is standalone. Full ERTS type selection and linkage, thread
progress and coordinated shutdown remain open. No FIFO or starvation
guarantee is added. Callers must pair the two access modes with their
corresponding releases. This campaign did not inject allocation failure
or waiter-admission rejection, and did not qualify broader backend
termination. Bounded negative observations confirm absence of completion
while startup holds the lock, without proving the instant of parking.

The earlier combined campaign's
[object-store fragmentation failure](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
remains unresolved. The synchronization and worker-libc profiles run in
separate fresh guests. Their success does not close that storage finding.

Repeat both profiles with new output directories.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-mutex --smp 2 --output target/erts/rwmutex-validation
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/rwmutex-regression
```
