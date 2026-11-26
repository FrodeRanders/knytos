# AtomVM native diagnostics — 2026-11-26

The signed single-scheduler service profile exposes `knytos:log/2`.
Each valid invocation writes one Knytos EL0 log record tagged with the
Erlang-selected level. The 239-byte message limit leaves room for the
longest level prefix in the existing 256-byte record. The logging call
borrows the message for validation and formatting, then submits the
formatted record synchronously.

## Validation

The native-service fixture observed the request slot occupied before
and after logging. Its domain emitted one record per level, including
a multibyte UTF-8 character and the full 239-byte warning. Invalid
level, message type, UTF-8, control character, Unicode line separator,
and 240-byte inputs raised `badarg`.

The signed two-CPU AArch64 QEMU 11.1.3 campaign passed 14 cases and
three rejection controls. The service worker joined with zero remaining
libc allocations, and signed node shutdown produced the QMP
`guest-shutdown` event and QEMU exit status zero. The host Rust suite
passed 77 unit tests and one compile-fail doctest.

The [run summary](../../plans/evidence/erlang-native-diagnostics/summary.json)
identifies the tested binaries, deployment descriptors and inputs. The
[serial excerpts](../../plans/evidence/erlang-native-diagnostics/serial-excerpts.txt)
record the diagnostic lines and shutdown.
