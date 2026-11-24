# AtomVM single-scheduler semantics — 2026-11-24

M2 was committed and pushed to `origin/feature/erlang-runtime` as
`d3e857be9e055c5167d4e11739cc9c6db001bf2e`. The following M3 work qualifies
core Erlang behavior on the signed native AtomVM adapter. Native AtomVM SMP
still requires a thread-safe libc and scheduler synchronization profile.

Normal AArch64 deployment authenticates the ELF containing the package. The
pinned AtomVM 0.6.6 reference interpreter runs the same package bytes.
OTP 27.3.4.19 runs the same test source with its own library modules. All three exercise the
transient worker policy in a `one_for_one` supervisor. The fixture requires
the old worker to answer a call and exit abnormally, observes its exact
monitor reference, then requires a different worker to answer. Both replies
occur within one VM and, on Knytos, one signed domain. The abnormal Erlang
process reports in the log are expected. See the
[source](../../../ports/atomvm/tests/semantics.erl) and
[reproduction commands](../../../ports/atomvm/README.md#erlang-semantics-and-worker-recovery).

## Native results

Each QEMU 11.1.3 run launched 14 signed cases and passed all three negative
build/signature controls. The cases included ten C regressions, AtomVM hello,
unavailable UTC, the semantic fixture and absolute deadline overflow. The C
survivor ran after both expected VM aborts.

| Guest CPUs | AtomVM schedulers | Passed cases | Semantic fixture CQ waits and deadline returns |
| ---: | ---: | ---: | --- |
| 1 | 1 | 14 | 7 waits, 7 deadlines |
| 2 | 1 | 14 | 7 waits, 7 deadlines |
| 4 | 1 | 14 | 7 waits, 7 deadlines |

The same 37,884-byte AVM package and 736,344-byte signed semantic ELF ran in
all three guests. Its signed SHA-256 was
`1223fa15fffc68057f525f40cd7ab4cab7942a8f9847de2bd278bac28ddaa1bf`.
Each run observed a 32,344-byte allocator peak and a sampled stack depth of
1,808 bytes. The signed stack ceiling was 256 KiB. Stack samples give a lower
bound. Terminal C cleanup released 59 remaining local allocations and left
zero tracked blocks. Reusing the VM within one domain would need separate
teardown qualification.

The semantic run entered a kernel CQ wait on each idle timer interval and
observed its deadline. The results are from finite fixtures. Full idle
utilization and longer-running workload measurements are open. See the
[measurements, exact artifact hashes and input identities](../../plans/evidence/erlang-m3/summary.json).

## Compatibility observations

| Behavior | Native AtomVM and generic Unix reference | OTP 27.3.4.19 |
| --- | --- | --- |
| Values and exceptions | Passed pattern matching, recursive integer lists, arithmetic, tuples, maps, binary parsing, exception handling and retained values through explicit GC | Passed the same fixture |
| Processes | Passed spawning, registration, selective receive, monitored and linked exit, registration removal and liveness observation | Passed the same fixture |
| Timers | Passed receive cancellation by message, repeated 45 ms waits, two processes with equal relative timeouts and cancellation of a 3,000,000,000 ms receive | Passed the same fixture |
| Supervision | Passed the tested tuple-form `one_for_one` and transient worker policy. The pinned AtomVM implementation ignores restart intensity and period. | Passed the same fixture. Full OTP supervisor behavior was outside this test. |
| ETS | No ETS implementation in the pinned release's VM NIF or library source | Separate host insert/lookup/delete probe passed |
| `erlang:send_after/3` and `erlang:cancel_timer/1` | Upstream wrappers call `timer_manager`, which uses wall-clock time. The Knytos package omits it and uses monotonic receive timers. | Separate host cancellation probe passed |
| Absolute deadline overflow | Knytos aborts before queuing a duration that fits in nanoseconds but overflows when added to current time. This is a Knytos adapter check. | Native overflow fixture outside the compatibility comparison |

The adapter checks the proposed duration against a monotonic sample taken
after the pinned scheduler's sample. This prevents both multiplication and
absolute-expiry overflow. The negative fixture starts a short receive, then
requests 18,446,744,073,709 ms. The campaign requires the explicit overflow
diagnostic and a subsequent successful deployment. See the
[negative fixture](../../../ports/atomvm/tests/deadline_overflow.erl).

The host comparisons use the following commands from the checkout, after
building the native fixture and its host tools:

```sh
target/atomvm/atomvm-semantics.atomvm-build/host/src/AtomVM \
  target/atomvm/atomvm-semantics.atomvm-build/application.avm
.dependencies/otp-27/bin/erl +S 1 -noshell -eval '
  {ok, semantics, Bin} =
    compile:file("ports/atomvm/tests/semantics.erl", [binary,report]),
  {module, semantics} = code:load_binary(semantics, "semantics.erl", Bin),
  ok = semantics:start(),
  T = ets:new(probe, [set]),
  true = ets:insert(T, {key,value}),
  [{key,value}] = ets:lookup(T,key),
  true = ets:delete(T),
  Ref = erlang:send_after(50,self(),unexpected),
  Left = erlang:cancel_timer(Ref),
  true = is_integer(Left),
  receive unexpected -> halt(1) after 60 -> ok end,
  erlang:display({otp, ets_and_cancel_timer, ok}),
  halt().'
```

## Kernel regression and remaining scheduler work

The standard AArch64 release test image passed all 17 registered kernel
self-tests with four guest CPUs. Its scheduler regression verified timer
affinity, certified runtime migration and safe cross-CPU retirement. The
kernel SHA-256 was
`20453efcf81b58a81e805a18c5d4af2c549d7b4ecbe2a72d955519421ff020d8`.
The diagnostic, boot profile and CI runtime input scripts passed 30 checks.

```sh
scripts/run-aarch64.sh release --tests --instance avm-m3-kernel --smp 4 --timeout 180
python3 scripts/tests/test-boot-diagnostics.py
python3 scripts/tests/test-boot-profiles.py
python3 scripts/tests/test-ci-runtime-inputs.py
```

The guest CPU matrix exercises one native AtomVM scheduler. True AtomVM SMP,
concurrent CQ producer stress, idle CPU utilization, multi-scheduler
throughput, wake latency distributions and shutdown fanout remain unqualified.
The next scheduler work needs generation-bound worker ownership, a
condition-wait contract and thread-safe C library state. See the
[multicore plan](../../plans/erlang-m3-multicore.md).

## Build and reference diagnostics

The first compiler invocation rejected a fixture helper that collided with
the auto-imported `processes/0` BIF. The first compiled Unix comparison then
failed because the package omitted AtomVM's `erlang` wrappers for spawning.
Renaming the helper and including the pinned wrappers resolved these fixture
failures. The original incomplete package was overwritten during the
corrected build. Its retained reference log records the unresolved module
and `undef` result. The evidence keeps these preparation failures separate
from the successful runs. See the
[evidence directory](../../plans/evidence/erlang-m3).
