# ERTS native configure thread selection

Observed 2026-11-27 on `feature/erlang-runtime`, based on
`b0663a87306f9099c8fec49f551a959db784dec6` plus the source hashes in the
[evidence manifest](../../plans/evidence/erts-native-configure/summary.json).

The explicit native configure probe now passes OTP's thread-library gate.
Headless configuration stops at `No wall clock source found`. Configure
still exits 1; no complete configured Make graph or ERTS emulator is built.
The default probe retains its original pthread-library rejection.

## Reproduce and inspect

```sh
python3 ports/erts/configure.py --native-ethread \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output /fresh/native-configure
```

Use a new output directory without whitespace. Inputs remain OTP 29.1.2 at
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d` and clean Picolibc 1.8.12 at
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`.

The probe exports pinned Git objects, builds/checks the existing native
archive and selected headers, and stages the native Autoconf macro and
versioned source/template patch. Host GNU Autoconf 2.73 regenerates the
exported `erts/configure`. The original OTP checkout is not patched.
The native invocation adds `--with-knytos-ethread=../sdk` and the supported
`--without-termcap` option for headless execution. It supplies no successful
POSIX feature answers or cache overrides.

## Results and qualification scope

| Probe | Thread result | Terminal result |
| --- | --- | --- |
| Default | No pthread library found | Exit 1 at the original thread gate |
| Initial native, termcap enabled | Native compile/link succeeds | Exit 1: no curses library functions |
| Final native, headless | Native compile/link succeeds | Exit 1: no wall-clock source |

The [initial termcap capture](../../plans/evidence/erts-native-configure/initial-termcap-enabled-configure.txt)
is preserved separately. Termcap remains unavailable; the final native probe
does not request it. That intermediate run did not retain its successful
link ELF. The final headless run retains and independently validates its
own [actual Autoconf source](../../plans/evidence/erts-native-configure/native-link.c)
and [retained-reference header](../../plans/evidence/erts-native-configure/ethread-link-check.h).

The native macro performs actual target size checks and compile-links all
294 checked public functions plus 33 native callable names. Inspection of
the resulting static AArch64 ELF confirms all 327 references remain present;
the C SDK's loader-layout checks pass. Omitting the native archive produces
undefined `ethr_*` symbols. Selecting pthreads alongside Knytos produces the
expected backend-conflict compile error. The existing four interface
signature/layout/backend/profile controls also reject.

GNU Make evaluates the staged native source-selection conditional to the
nine native sources, while its pthread branch still selects the original
six sources. Compiling those copied native sources with the qualified SDK
reproduces the archive byte for byte. This checks source routing and an
independent staged-source build, not the full OTP Make graph. Configure has
not reached `AC_OUTPUT`.

The archive is identical to the one used by the earlier
[21-case bootstrap and 28-case header runtime qualification](2026-11-27-erts-native-bootstrap.md).
No native runtime source changed and no new guest run is claimed here.
The [final native configure capture](../../plans/evidence/erts-native-configure/native-configure.txt)
and [default control](../../plans/evidence/erts-native-configure/default-configure.txt)
retain the observed gates. Complete input manifests, SDK hashes, source-list
results, link/negative controls and compiler identities are in the evidence.

| Artifact | SHA-256 |
| --- | --- |
| Qualified and staged native archive | `0ef39934fc1a745557a38ad46300e8b969bd9f50a01a2361b70b116d5bdc9fba` |
| Actual Autoconf link ELF | `de7c2e245ba55fbba30df904a0861c2e511dfb5e6ece3de4ce6fe01311ac8642` |
| Regenerated `erts/configure` | `2c334f04dd1edd71a4b636f839850ef943560685687a4fb75cd00adc08d99aff` |

## Remaining work

The C SDK supplies checked monotonic time, while its realtime API still
reports unavailable. OTP's POSIX wall-clock probes therefore do not supply
a clock source. A native ERTS clock backend must use actual supported time
semantics; declaring a successful POSIX clock check would not implement it.

CPU/runtime data, actual stack bounds, native thread capacity/progress,
top-level configure propagation and the remaining platform/build interfaces
remain open. The qualified runtime still has three worker records with
default stacks. The
[earlier object-store extent failure](2026-11-26-erts-mutex-adapter.md#failed-combined-campaign)
also remains unresolved; compile/link success neither explains nor closes it.
