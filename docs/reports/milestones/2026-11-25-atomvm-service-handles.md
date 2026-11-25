# AtomVM reusable service handles — 2026-11-25

The single-scheduler service profile adds asynchronous `knytos:lookup/1`.
A successful reply carries an AtomVM resource bound to the process that
looked it up. The caller can use that resource in `knytos:request/3` and
invalidate it with `knytos:close/1`. One reusable connection and one
outstanding request fit in the fixed worker scope.

The worker holds the connection as a Rust owner and releases it after
any accepted call settles. Explicit close, garbage collection and owner
exit all detach the resource. The finalizer publishes release intent,
and the scheduler signals the worker outside VM guards. Startup joins
the worker before acknowledging shutdown.

The first signed run stopped at `lookup/1` with `badarg`. Two unbraced
checks used a multi-statement error macro, so the first return executed
unconditionally. Both checks now use blocks. [First run evidence](../../plans/evidence/erlang-native-service-handles/first-run-failure.json)

## Validation

The corrected signed two-CPU AArch64 QEMU run passed 14 cases and three
rejection controls. The native fixture verified signed grant denial, two
calls through one handle, and a `busy` result for a second lookup. A
different process could neither call nor close the handle. An external
term round trip yielded an inert value that request validation rejected.
The fixture also confirmed that an old handle remained stale after
replacement and that explicit close, garbage collection and owner exit
each made the slot available again.

The shutdown fixture kept a service handle while another Erlang process
submitted work. The worker joined and the guest acknowledged signed
shutdown. QMP reported `guest-shutdown` and QEMU exited with status zero.
The host run passed 75 unit tests and one compile-fail doctest.

The [run summary](../../plans/evidence/erlang-native-service-handles/summary.json)
records the signed artifacts and the [serial excerpts](../../plans/evidence/erlang-native-service-handles/serial-excerpts.txt)
show their runtime observations.
