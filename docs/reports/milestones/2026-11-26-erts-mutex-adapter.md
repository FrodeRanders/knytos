# ERTS mutex adapter — 2026-11-26

The [mutex adapter](../../../ports/erts/README.md#mutex-adapter) implements
the six OTP 29.1.2 mutex entry points over native synchronization. Mutex
lock uses a new event-only acquisition path. It shares the timed path's
readiness guard, exact participant registration and unlock notification,
and retains its object until acquisition or terminal rejection.

Trylock reports `EBUSY` for a held mutex, including a same-thread attempt.
Busy destruction retains the object. Successful destruction clears the
wrapper pointer. Unexpected lock or unlock errors abort the domain after
native waiter-interest cleanup. Optional spin counts go directly to the
registered wait. The enclosing backend must supply unlock progress and
coordinate shutdown before releasing mutex storage.

## Qualification

The focused two-CPU AArch64 QEMU campaign passed 19 signed cases and three
rejection controls. Its positive fixture checked cross-CPU contention,
payload publication, same-thread and foreign-thread trylock rejection,
busy destruction, successful disposal and zero remaining libc allocations.
Its foreign-unlock fixture entered the expected fatal adapter and C abort
paths. A later signed C application completed successfully.

The [focused summary](../../plans/evidence/erts-mutex-adapter/focused/summary.json)
records fixture identities, source hashes and pinned upstream declarations.
The [serial excerpts](../../plans/evidence/erts-mutex-adapter/focused/serial-excerpts.txt)
retain the mutex and abort markers.

The separate worker-libc regression passed 32 signed cases and three
rejection controls, including timed synchronization, condition-variable
reacquisition and the earlier ERTS adapters. Its
[summary](../../plans/evidence/erts-mutex-adapter/regression/summary.json)
records the tested artifacts and build inputs. Host validation passed
77 unit tests and one compile-fail documentation test. Rust formatting,
Python syntax and diff checks passed.

## Failed combined campaign

The initial 34-case combined campaign failed while admitting the
foreign-unlock fixture. Thirty-two case expectations had passed, including
the positive mutex fixture. Foreign-unlock and the final survivor case
were not completed. Catalog persistence reported an object-store write
status of 2 (`ERR_NO_SPACE`) for a 9,583-byte log record, and deployment
admission returned HTTP 503 with error -22.

The captured disk bitmap contained 11,256 free 512-byte blocks, about
5.5 MiB. After reserving the next object header, the first sixteen free
runs provided eighteen blocks. The log needed nineteen data blocks.
`allocate_fragmented` consumes runs in increasing block order and stops
at sixteen extents before reaching the large free run. This source and
bitmap comparison identifies an extent-selection limitation consistent
with the observed write rejection. It does not establish a general
storage-capacity limit or qualify recovery from the fatal persistence path.

The [failed summary](../../plans/evidence/erts-mutex-adapter/failed-combined/summary.json)
preserves the free-run list, disk and executable identities, storage
source hashes, passed cases and admission diagnostic. The
[failed serial excerpts](../../plans/evidence/erts-mutex-adapter/failed-combined/serial-excerpts.txt)
retain the successful mutex result and subsequent persistence failure.
Raw disk and full build logs are outside this version-controlled record.
The smaller campaigns ran in fresh guests. The storage fragmentation
failure remains unresolved, and their success does not close it.

## Remaining integration

These are standalone adapters. Full ERTS selection and linkage,
condition variables, thread progress and coordinated backend shutdown
remain open. Mutex destruction still requires externally synchronized
lifetime. The fixture's bounded negative observation confirms that the
worker did not complete while startup held the mutex, without proving
the exact moment when the worker parked.

Repeat both campaigns with fresh output directories.

```sh
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --erts-mutex --smp 2 --output target/erts/mutex-validation
python3 ports/libc/run-tests.py --picolibc-source /path/to/picolibc \
  --worker-libc --smp 2 --output target/erts/mutex-regression
```
