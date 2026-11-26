#!/usr/bin/env python3
"""Build the standalone AArch64 ERTS prerequisites as a static archive."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("ethr_event.c", "ethr_tsd.c", "ethr_thread.c", "ethr_mutex.c",
           "ethr_rwmutex.c", "ethr_lock.c")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*arguments):
    subprocess.run([str(arg) for arg in arguments], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--picolibc-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or output.with_suffix(".build.json").exists():
        parser.error("use a new archive output path")
    work = output.with_suffix(".build")
    work.mkdir(parents=True, exist_ok=False)
    control = work / "sdk-control.elf"
    run("python3", ROOT / "ports/libc/build.py", ROOT / "ports/libc/tests/empty.c",
        "--worker-libc", "--picolibc-source", args.picolibc_source.resolve(),
        "--output", control)
    sdk = json.loads(control.with_suffix(".build.json").read_text())
    libc_headers = ROOT / "target/c-runtime/picolibc-aarch64-workers-v1/picolibc/include"
    flags = ["--target=aarch64-none-elf", "-std=c11", "-ffreestanding", "-fno-builtin",
             "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-outline-atomics",
             "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-Os",
             "-Wall", "-Wextra", "-Werror", "-DKNYTOS_NATIVE_WORKERS",
             "-D__STDIO_LOCKING", "-DKNYTOS_WORKER_LIBC",
             "-isystem", str(libc_headers), "-I", str(ROOT / "ports/libc/include")]
    objects = []
    for name in SOURCES:
        obj = work / Path(name).with_suffix(".o")
        run("clang", *flags, "-c", ROOT / "ports/erts" / name, "-o", obj)
        objects.append(obj)
    rust = subprocess.check_output(["rustc", "-vV"], cwd=ROOT, text=True)
    host = next(line.removeprefix("host: ") for line in rust.splitlines() if line.startswith("host: "))
    sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], cwd=ROOT, text=True).strip())
    archiver = sysroot / "lib/rustlib" / host / "bin/llvm-ar"
    run(archiver, "rcs", output, *objects)
    inputs = dict(sdk["inputs_sha256"])
    sources = [ROOT / "ports/erts" / name for name in SOURCES]
    sources.extend(sorted((ROOT / "ports/erts").glob("*.h")))
    sources.append(Path(__file__).resolve())
    inputs.update({str(path.relative_to(ROOT)): digest(path) for path in sources})
    evidence = {key: sdk[key] for key in ("picolibc_revision", "clang", "rustc")}
    evidence.update(schema="knytos.erts-prerequisites.v1", sha256=digest(output),
                    bytes=output.stat().st_size, profile="aarch64-static-c11-worker-libc-no-tls",
                    inputs_sha256=inputs, compiler_flags=flags,
                    members_sha256={obj.name: digest(obj) for obj in objects},
                    sdk_control_sha256=sdk["sha256"])
    output.with_suffix(".build.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Built standalone ERTS prerequisites {output}")


if __name__ == "__main__":
    main()
