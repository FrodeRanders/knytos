# M2 implementation scope

The user requested that M0/M1 be committed and pushed, then authorized proceeding
with M2 on `feature/erlang-runtime`. Commit `53b253a7` is on origin.

1. Build the independently pinned OTP 27.3.4.19 host compiler, commit
   `fc7e10af595f3ffd2c25c84c253a4d042734cfed`, and AtomVM 0.6.6 at
   `ff993a80963298b532c1e573f883951ecaac9fef`. Keep upstream VM sources clean.
2. Add a Knytos single-scheduler platform over M1: allocator/console, checked
   monotonic time, static NIF registration, immutable package lookup and a sole
   consumer of launch CQ zero. No new kernel mechanism or owning C capability.
3. Compile an explicit minimal Erlang/library closure with the compatible
   compiler, package using upstream PackBEAM, and embed it in read-only signed
   ELF memory. No external module path or unauthenticated package loading.
4. Validate hello, native platform identity, queued-message receive, a timed
   receive, and CQ wake-before-wait. Compare the same package on the pinned
   generic Unix interpreter with the expected platform identity difference.
5. Make missing facilities explicit. The upstream void wall-clock hook aborts
   with an unavailable diagnostic until a reviewed authorized UTC policy exists;
   test this failure and subsequent deployment isolation. Reject unsupported
   listeners/select operations and file/package access honestly.
6. Run signed AArch64 deployments, retain relevant C/Rust regression coverage,
   record package/ELF/heap/stack observations and failed attempts separately,
   and publish a completion report before progressing to M3.

This scope adds no TLS, SMP, general native service NIFs, network/filesystem
compatibility, dynamic loading, JIT, ERTS or state migration.
