#!/usr/bin/env python3
"""Build the standalone AArch64 ERTS prerequisites as a static archive."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("ethr_event.c", "ethr_tsd.c", "ethr_thread.c", "ethr_mutex.c",
           "ethr_rwmutex.c", "ethr_lock.c", "ethr_ts_event.c", "ethr_atomic_info.c",
           "ethr_diagnostics.c")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*arguments):
    subprocess.run([str(arg) for arg in arguments], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--picolibc-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    profile = parser.add_mutually_exclusive_group()
    profile.add_argument("--monotonic-clock", action="store_true",
                        help="build the independent two-function native clock archive")
    profile.add_argument("--system-clock", action="store_true",
                         help="build monotonic and published UTC clock prerequisites")
    profile.add_argument("--platform-clock", action="store_true",
                         help="also build the pinned time-initialization contract probe")
    parser.add_argument("--otp-source", type=Path)
    parser.add_argument("--clock-sample-age-ms", type=int, default=5000)
    args = parser.parse_args()
    if args.platform_clock and args.otp_source is None:
        parser.error("--platform-clock requires --otp-source")
    if not 1 <= args.clock_sample_age_ms <= 2147483647:
        parser.error("clock sample age must be 1..2147483647 milliseconds")
    output = args.output.resolve()
    if output.exists() or output.with_suffix(".build.json").exists():
        parser.error("use a new archive output path")
    work = output.with_suffix(".build")
    work.mkdir(parents=True, exist_ok=False)
    control = work / "sdk-control.elf"
    run("python3", ROOT / "ports/libc/build.py", ROOT / "ports/libc/tests/empty.c",
        "--service-worker" if args.system_clock or args.platform_clock else "--worker-libc",
        "--picolibc-source", args.picolibc_source.resolve(),
        "--output", control)
    sdk = json.loads(control.with_suffix(".build.json").read_text())
    libc_headers = ROOT / "target/c-runtime/picolibc-aarch64-workers-v1/picolibc/include"
    flags = ["--target=aarch64-none-elf", "-std=c11", "-ffreestanding", "-fno-builtin",
             "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-outline-atomics",
             "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-Os",
             "-Wall", "-Wextra", "-Werror", "-DKNYTOS_NATIVE_WORKERS",
             "-D__STDIO_LOCKING", "-DKNYTOS_WORKER_LIBC",
             "-isystem", str(libc_headers), "-I", str(ROOT / "ports/libc/include")]
    sources_selected = ("erts_monotonic.c",) if args.monotonic_clock else SOURCES
    if args.system_clock or args.platform_clock:
        sources_selected = ("erts_monotonic.c", "erts_system_time.c")
        flags.append("-DKNYTOS_SERVICE_WORKER")
    time_contract = None
    if args.platform_clock:
        from stage_time import stage
        time_contract = stage(args.otp_source.resolve(), work / "time-contract")
        sources_selected += ("erts_time_init.c",)
        flags.extend(("-DKNYTOS_ERTS_TIME_CONTRACT_PROBE",
                      f"-DERTS_KNYTOS_CLOCK_MAX_AGE_NS={args.clock_sample_age_ms * 1000000}ULL",
                      "-I", str(ROOT / "ports/erts"),
                      "-I", str(work / "time-contract")))
    objects = []
    for name in sources_selected:
        obj = work / Path(name).with_suffix(".o")
        run("clang", *flags, "-c", ROOT / "ports/erts" / name, "-o", obj)
        objects.append(obj)
    rust = subprocess.check_output(["rustc", "-vV"], cwd=ROOT, text=True)
    host = next(line.removeprefix("host: ") for line in rust.splitlines() if line.startswith("host: "))
    sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], cwd=ROOT, text=True).strip())
    archiver = sysroot / "lib/rustlib" / host / "bin/llvm-ar"
    run(archiver, "rcs", output, *objects)
    inputs = dict(sdk["inputs_sha256"])
    sources = [ROOT / "ports/erts" / name for name in sources_selected]
    sources.extend(sorted((ROOT / "ports/erts").glob("*.h")))
    sources.append(Path(__file__).resolve())
    if args.platform_clock:
        sources.append(ROOT / "ports/erts/stage_time.py")
    inputs.update({str(path.relative_to(ROOT)): digest(path) for path in sources})
    evidence = {key: sdk[key] for key in ("picolibc_revision", "clang", "rustc")}
    evidence.update(schema=("knytos.erts-platform-clock.v1" if args.platform_clock else
                            "knytos.erts-system-clock.v1" if args.system_clock else
                            "knytos.erts-monotonic.v1" if args.monotonic_clock
                            else "knytos.erts-prerequisites.v1"), sha256=digest(output),
                    bytes=output.stat().st_size,
                    profile=("aarch64-static-c11-service-worker-no-tls" if args.system_clock or args.platform_clock
                             else "aarch64-static-c11-worker-libc-no-tls"),
                    inputs_sha256=inputs, compiler_flags=flags,
                    members_sha256={obj.name: digest(obj) for obj in objects},
                    sdk_control_sha256=sdk["sha256"])
    if time_contract:
        evidence.update(time_contract=time_contract, clock_sample_age_ms=args.clock_sample_age_ms)
    output.with_suffix(".build.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Built standalone ERTS prerequisites {output}")


if __name__ == "__main__":
    main()
