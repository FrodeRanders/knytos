# ERTS AArch64 target configuration — 2026-11-26

OTP 29.1.2 now recognizes `aarch64-unknown-knytos` in the staged
configure probe and links its C test executable with the current
Knytos worker-libc SDK. Configure stops at its native thread-library
requirement.

## Reproduce

From the repository root, point the probe at OTP 29.1.2 commit
`d199a49a01a2079afb6f2c5c9ae047cd4555e68d` and Picolibc 1.8.12
commit `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`.

```sh
python3 ports/erts/configure.py \
  --otp-source /path/to/otp29 --picolibc-source /path/to/picolibc \
  --output target/erts/preflight
```

Use a new output directory without whitespace in its path. The
probe exports the pinned OTP tree and changes only the staged
`make/autoconf/config.sub` OS recognition. It builds the existing
worker-libc control and passes its Clang target, Picolibc headers,
startup archive, linker script and LLVM tools to `erts/configure`.
The configure run starts with an empty cache and no site answers.
The command returns the configure exit status and saves the
diagnostics in `configure.txt` and `build/config.log`. The output also
includes the SDK hashes and configure arguments in `result.json`.

## Observed result

The AArch64 Darwin host used Apple Clang 21.0.0 and the repository's
pinned Rust toolchain. The unsigned AArch64 control ELF had SHA-256
`3486bf6880d4d4b9b21796ea4fa3d59afd75c4e26accee2e55dce9b5c6616d9e`.
The adapted `config.sub` had SHA-256
`a23a5c0d03a530bafdc7e0d7e1b9fa473c38eb493733f109a0d52a19c1d0441c`.

```text
checking host system type... aarch64-unknown-knytos
checking target system type... aarch64-unknown-knytos
checking whether the C compiler works... yes
checking whether we are cross compiling... yes
checking size of void *... 8
checking for pthread_create in -lpthread... no
checking for pthread_create in -lc_r... no
checking for pthread_create... no
configure: error: cannot build emulator since no thread library was found
```

Configure exited 1. The target probes also reported little-endian integer
ordering and left double-word ordering indeterminate. The current SDK
does not supply the POSIX clock interfaces probed by OTP. This result
qualifies target canonicalization and C linkage only.

The thread-library gate requires a Knytos `ethread` backend or a
separately qualified compatibility implementation. The
[M4 preflight](2026-11-26-atomvm-m4-completion.md#erts-prerequisite-probes)
records why the present four-participant roster also needs a broader
ownership and stack-admission design for ERTS.
