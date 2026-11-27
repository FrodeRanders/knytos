#!/usr/bin/env python3
"""Build one static, TLS-free AArch64 C application; signing is explicit."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PIN = "2ae376c6cdf4fef90ca2388ecf7a07457fa63cff"


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def check_elf(path, *, relocatable=False):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("expected little-endian ELF64")
    if struct.unpack_from("<HH", data, 16) != (1 if relocatable else 2, 183):
        raise ValueError("expected AArch64 ET_REL/ET_EXEC")
    phoff, shoff = struct.unpack_from("<QQ", data, 32)
    phsize, phnum, shsize, shnum = struct.unpack_from("<HHHH", data, 54)
    if shsize != 64 or shoff + shnum * shsize > len(data):
        raise ValueError("invalid section table")
    for index in range(shnum):
        pos = shoff + index * shsize
        typ, flags = struct.unpack_from("<IQ", data, pos + 4)
        size = struct.unpack_from("<Q", data, pos + 32)[0]
        if flags & 0x400 and size:
            raise ValueError("TLS is unsupported by the single-threaded C profile")
        if not relocatable and typ in (4, 9) and size:
            raise ValueError("runtime relocations are unsupported")
    if relocatable:
        return
    if len(data) > 4 * 1024 * 1024 or phsize != 56 or phoff + phnum * phsize > len(data):
        raise ValueError("invalid or oversized executable")
    ranges = []
    entry = struct.unpack_from("<Q", data, 24)[0]
    executable_entry = False
    # Canonical loader-reserved launch/heap/shard pages. This build does not
    # redefine them; admission remains enforced independently by the kernel.
    reserved = [(0x10000, 0x13000), (0x300000, 0x7f1000), (0x800000, 0x804000)]
    for index in range(phnum):
        typ, flags, offset, address, _, filesz, memsz, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + index * phsize)
        if typ in (2, 3, 7):
            raise ValueError("dynamic loading or TLS program header")
        if typ != 1 or memsz == 0:
            continue
        if flags & 3 == 3 or filesz > memsz or offset + filesz > len(data) or offset % 4096 != address % 4096:
            raise ValueError("invalid LOAD layout or writable executable memory")
        start, end = address & ~4095, (address + memsz + 4095) & ~4095
        if start < 0x20000 or end >= 0x0000400000000000 or any(start < b and end > a for a, b in ranges + reserved):
            raise ValueError("LOAD overlaps reserved or previously loaded pages")
        ranges.append((start, end))
        executable_entry |= bool(flags & 1 and address <= entry < address + memsz)
    if not ranges or not executable_entry:
        raise ValueError("entry is not in executable LOAD memory")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--picolibc-source", type=Path, default=ROOT / ".dependencies/picolibc")
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--include", action="append", default=[], type=Path)
    parser.add_argument("--library", action="append", default=[], type=Path)
    profile = parser.add_mutually_exclusive_group()
    profile.add_argument("--native-workers", action="store_true",
                        help="enable the experimental worker ABI with restricted worker callbacks")
    profile.add_argument("--worker-libc", action="store_true",
                         help="enable worker ABI, participant errno, and fixed-stream locking")
    profile.add_argument("--service-worker", action="store_true",
                         help="enable worker libc and scoped scalar IPC on native workers")
    args = parser.parse_args()
    worker_libc = args.worker_libc or args.service_worker
    source = args.picolibc_source.resolve()
    if not source.exists() and args.fetch:
        source.parent.mkdir(parents=True, exist_ok=True)
        run("git", "clone", "--depth", "1", "--branch", "1.8.12", "https://github.com/picolibc/picolibc.git", source)
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True).strip()
    if revision != PIN or dirty:
        raise SystemExit("Picolibc must be the clean pinned 1.8.12 source; pass --picolibc-source or --fetch")
    work = ROOT / "target/c-runtime"
    libc = work / ("picolibc-aarch64-workers-v1" if worker_libc else "picolibc-aarch64-v1")
    libc_options = ["-D__SINGLE_THREAD=ON", "-D__GLOBAL_ERRNO=ON"]
    if worker_libc:
        libc_options = ["-D__SINGLE_THREAD=OFF", "-D__GLOBAL_ERRNO=OFF",
                        "-D__PICOLIBC_ERRNO_FUNCTION=knytos_errno_location",
                        "-DKNYTOS_WORKER_LIBC=ON"]
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    objects = output.parent / (output.stem + ".build")
    objects.mkdir(exist_ok=True)
    sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], cwd=ROOT, text=True).strip())
    host = subprocess.check_output(["rustc", "-vV"], cwd=ROOT, text=True).split("host: ")[1].splitlines()[0]
    llvm = sysroot / "lib/rustlib" / host / "bin"
    run("cmake", "-S", source, "-B", libc, "-G", "Ninja",
        "-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "ports/libc/aarch64-clang.cmake"),
        "-DCMAKE_BUILD_TYPE=MinSizeRel", "-DKNYTOS_LLVM_AR=" + str(llvm / "llvm-ar"),
        "-D__THREAD_LOCAL_STORAGE=OFF", *libc_options,
        "-DENABLE_MALLOC=OFF", "-DTESTS=OFF", "-DPOSIX_CONSOLE=OFF", cwd=ROOT)
    run("cmake", "--build", libc, "-j", "6")
    crate_feature = "service-worker" if args.service_worker else (
        "worker-libc" if worker_libc else "native-workers" if args.native_workers else None)
    run("cargo", "build", "--locked", "-p", "knytos-cabi", "--release",
        "--target", ROOT / "crates/knytos-services/aarch64-unknown-none.json",
        *(["--features", crate_feature] if crate_feature else []),
        "-Z", "build-std=core,alloc,compiler_builtins", cwd=ROOT)
    common = ["clang", "--target=aarch64-none-elf", "-std=c11", "-ffreestanding", "-fno-builtin",
              "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-outline-atomics",
              "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-Os", "-Wall", "-Wextra", "-Werror",
              "-isystem", str(libc / "picolibc/include"), "-I", str(ROOT / "ports/libc/include")]
    common.extend(flag for directory in args.include for flag in ("-I", str(directory.resolve())))
    if crate_feature:
        common.append("-DKNYTOS_NATIVE_WORKERS")
    if worker_libc:
        common.extend(("-D__STDIO_LOCKING", "-DKNYTOS_WORKER_LIBC"))
    if args.service_worker:
        common.append("-DKNYTOS_SERVICE_WORKER")
    run(*common, *["-D" + d for d in args.define], "-Dmain=knytos_c_main", "-c", args.source.resolve(), "-o", objects / "application.o")
    check_elf(objects / "application.o", relocatable=True)
    run(*common, "-c", ROOT / "ports/libc/bridge.c", "-o", objects / "bridge.o")
    linker = (ROOT / "crates/knytos-services/link.x").read_text().replace(
        ".rodata : { *(.rodata .rodata.*) } :rodata",
        ".rodata : { *(.rodata .rodata.*) . = ALIGN(8); "
        "__init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; "
        "__fini_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array)) "
        "KEEP(*(.fini_array_onexit)) __fini_array_end = .; } :rodata")
    (objects / "link.x").write_text(linker)
    link_map = ["-Map=" + str(objects / "link-map.txt")] if worker_libc else []
    run(llvm / "rust-lld", "-flavor", "gnu", "-m", "aarch64elf", "--gc-sections", "--entry=_start", *link_map,
        "-T", objects / "link.x", "-o", output, "--start-group", objects / "application.o", objects / "bridge.o",
        *[p.resolve() for p in args.library],
        ROOT / "target/aarch64-unknown-none/release/libknytos_cabi.a", libc / "libc.a", "--end-group")
    check_elf(output)
    run(llvm / "llvm-strip", "--strip-debug", output)
    check_elf(output)
    notices = output.parent / "picolibc-COPYING.txt"
    shutil.copyfile(source / "COPYING.picolibc", notices)
    evidence = {"picolibc_revision": PIN, "clang": subprocess.check_output(["clang", "--version"], text=True).strip(),
                "rustc": subprocess.check_output(["rustc", "-vV"], cwd=ROOT, text=True).strip(),
                "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "bytes": output.stat().st_size,
                "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
                "defines": args.define, "profile": "aarch64-static-c11-single-thread-no-tls", "signed": False}
    if args.native_workers:
        evidence["profile"] = "aarch64-static-c11-native-workers-restricted-libc-no-tls"
    if worker_libc:
        evidence["profile"] = "aarch64-static-c11-worker-libc-no-tls"
        evidence["link_map_sha256"] = hashlib.sha256((objects / "link-map.txt").read_bytes()).hexdigest()
    if args.service_worker:
        evidence["profile"] = "aarch64-static-c11-service-worker-no-tls"
    evidence["libraries_sha256"] = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in args.library}
    inputs = [ROOT / "Cargo.lock", ROOT / "Cargo.toml", ROOT / "ports/libc/build.py",
              ROOT / "ports/libc/bridge.c", ROOT / "ports/libc/include/knytos.h",
              ROOT / "ports/libc/aarch64-clang.cmake", ROOT / "crates/knytos-services/link.x",
              ROOT / "crates/knytos-services/aarch64-unknown-none.json", ROOT / "crates/knytos-cabi/Cargo.toml"]
    crates = ["knytos-cabi", "knytos-rt", "knytos-launch", "knytos-lifecycle", "knytos-syscall"]
    if args.service_worker:
        for crate in ("knytos-app", "knytos-protocol-grant", "knytos-protocol-time"):
            crates.append(crate)
            inputs.append(ROOT / "crates" / crate / "Cargo.toml")
    for crate in crates:
        inputs.extend((ROOT / "crates" / crate / "src").rglob("*.rs"))
    evidence["inputs_sha256"] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)}
    output.with_suffix(".build.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Built {output}; signing is a separate step")


if __name__ == "__main__":
    main()
