#!/usr/bin/env python3
"""Probe the pinned ERTS configure script against the AArch64 Knytos C SDK."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OTP_PIN = "d199a49a01a2079afb6f2c5c9ae047cd4555e68d"
TARGET = "aarch64-unknown-knytos"


def capture(*argv):
    return subprocess.check_output(argv, cwd=ROOT, text=True).strip()


def prepare_otp(source, tree):
    command = ["git", "-C", source, "archive", OTP_PIN]
    with subprocess.Popen(command, stdout=subprocess.PIPE) as archive:
        subprocess.run(["tar", "-xf", "-", "-C", tree], stdin=archive.stdout, check=True)
        archive.stdout.close()
        if status := archive.wait():
            raise subprocess.CalledProcessError(status, command)

    config_sub = tree / "make/autoconf/config.sub"
    original = config_sub.read_text()
    anchor = "\tnone)\n\t\t;;\n\tkernel* | msvc* )"
    if original.count(anchor) != 1:
        raise SystemExit("Unexpected config.sub in the pinned OTP tree")
    config_sub.write_text(original.replace(anchor, anchor.replace("none)", "none | knytos)")))
    return config_sub


def prepare_sdk(picolibc, out):
    control = out / "sdk-control.elf"
    subprocess.run(["python3", ROOT / "ports/libc/build.py", ROOT / "ports/libc/tests/empty.c",
                    "--worker-libc", "--picolibc-source", picolibc, "--output", control],
                   cwd=ROOT, check=True)
    sdk = out / "sdk"
    picolibc_build = ROOT / "target/c-runtime/picolibc-aarch64-workers-v1"
    shutil.copytree(picolibc_build / "picolibc/include", sdk / "include")
    shutil.copy2(ROOT / "ports/libc/include/knytos.h", sdk / "include/knytos.h")
    shutil.copy2(picolibc_build / "libc.a", sdk)
    shutil.copy2(ROOT / "target/aarch64-unknown-none/release/libknytos_cabi.a", sdk)
    for name in ("bridge.o", "link.x"):
        shutil.copy2(control.with_suffix(".build") / name, sdk)
    host = next(line.removeprefix("host: ") for line in capture("rustc", "-vV").splitlines()
                if line.startswith("host: "))
    tools = Path(capture("rustc", "--print", "sysroot")) / "lib/rustlib" / host / "bin"
    (sdk / "ld.lld").symlink_to(tools / "rust-lld")
    (sdk / "llvm-ar").symlink_to(tools / "llvm-ar")
    (sdk / "llvm-ranlib").symlink_to(tools / "llvm-ar")
    return sdk, control.with_suffix(".build.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--otp-source", type=Path, required=True)
    parser.add_argument("--picolibc-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = args.otp_source.resolve()
    if capture("git", "-C", source, "rev-parse", "HEAD") != OTP_PIN:
        parser.error("OTP source must be at the pinned 29.1.2 revision")
    out = args.output.resolve()
    if any(ch.isspace() for ch in str(out)):
        parser.error("the pinned OTP configure scripts require an output path without whitespace")
    out.mkdir(parents=True)
    tree, build = out / "otp", out / "build"
    tree.mkdir()
    build.mkdir()
    config_sub = prepare_otp(source, tree)
    sdk, control_manifest = prepare_sdk(args.picolibc_source.resolve(), out)
    build_target = capture(tree / "make/autoconf/config.guess")
    command = [tree / "erts/configure", f"--srcdir={tree / 'erts'}", f"--build={build_target}",
               f"--host={TARGET}", "--disable-jit"]
    # The SDK paths are relative to the configure directory so Autoconf's
    # shell-expanded argument lists never contain the host repository path.
    toolchain = {
        "ERL_TOP": str(tree),
        "CONFIG_SITE": "/dev/null",
        "CC": "clang --target=aarch64-none-elf --sysroot=../sdk",
        "CXX": "clang++ --target=aarch64-none-elf --sysroot=../sdk",
        "AR": "../sdk/llvm-ar",
        "RANLIB": "../sdk/llvm-ranlib",
        "LD": "../sdk/ld.lld",
        "CFLAGS": "-ffreestanding -fno-builtin -fno-pic -fno-pie -mno-outline-atomics",
        "CPPFLAGS": "-Dmain=knytos_c_main -DKNYTOS_NATIVE_WORKERS -D__STDIO_LOCKING "
                    "-DKNYTOS_WORKER_LIBC -isystem ../sdk/include",
        "CXXFLAGS": "-ffreestanding -fno-pic -fno-pie -mno-outline-atomics",
        "LDFLAGS": "--ld-path=../sdk/ld.lld -nostdlib -Wl,-T,../sdk/link.x,--entry=_start,--gc-sections",
        "LIBS": "-Wl,--start-group ../sdk/bridge.o ../sdk/libknytos_cabi.a ../sdk/libc.a -Wl,--end-group",
    }
    env = {name: value for name, value in os.environ.items()
           if not name.startswith(("ac_cv_", "erl_cv_", "erl_xcomp_"))}
    log = out / "configure.txt"
    with log.open("w") as stream:
        result = subprocess.run(command, cwd=build, env=env | toolchain,
                                stdout=stream, stderr=subprocess.STDOUT)
    evidence = {
        "otp_revision": OTP_PIN,
        "config_sub_sha256": hashlib.sha256(config_sub.read_bytes()).hexdigest(),
        "sdk_control_sha256": json.loads(control_manifest.read_text())["sha256"],
        "sdk_sha256": {name: hashlib.sha256((sdk / name).read_bytes()).hexdigest()
                       for name in ("bridge.o", "link.x", "libknytos_cabi.a", "libc.a")},
        "configure_arguments": [str(arg) for arg in command],
        "configure_status": result.returncode,
    }
    (out / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"ERTS configure exited {result.returncode}. See {log}")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
