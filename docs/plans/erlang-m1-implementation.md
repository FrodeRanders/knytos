# M1 implementation scope

The user authorized M1 after reviewing M0. Work stays on
`feature/erlang-runtime` and stops before AtomVM integration.

1. Build pinned Picolibc 1.8.12 for AArch64, with TLS and its allocator disabled.
   Use the existing CMake backend if it supplies the required profile.
2. Add a workspace C ABI/static runtime: Rust entry and heap, tracked C
   allocations, bounded console output, monotonic time, explicit unavailable
   UTC, constructor/finalizer dispatch and defined normal/error termination.
3. Build ordinary static ELF applications with a checked TLS-free layout.
   Keep signing explicit and use the normal authenticated deployment path.
4. Test allocator overflow/alignment/realloc/failure, clocks and ABI behavior
   on the host where meaningful. Execute signed C fixtures on AArch64 QEMU,
   including constructors, formatting, allocator pressure, normal exit and
   failure isolation. Preserve negative TLS/link controls and Rust baseline.
5. Publish exact source/tool inputs, native resource observations, results and
   remaining limitations. No AtomVM, POSIX subsystem, TLS, SMP or kernel
   mechanism changes belong to this milestone.
