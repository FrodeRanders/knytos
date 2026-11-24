#!/usr/bin/env python3
"""Build the pinned interpreter with one authenticated embedded AVM package."""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
ATOMVM_PIN = "ff993a80963298b532c1e573f883951ecaac9fef"
OTP_PIN = "fc7e10af595f3ffd2c25c84c253a4d042734cfed"


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--atomvm-source", required=True, type=Path)
    parser.add_argument("--otp-source", required=True, type=Path)
    parser.add_argument("--picolibc-source", required=True, type=Path)
    parser.add_argument("--worker-libc", action="store_true",
                        help="qualify the worker libc closure with the single-scheduler interpreter")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--application", type=Path, default=ROOT / "ports/atomvm/tests/hello.erl")
    parser.add_argument("--otp-behaviours", action="store_true",
                        help="include the pinned erlang wrappers, gen_server and supervisor")
    args = parser.parse_args()
    atomvm, otp = args.atomvm_source.resolve(), args.otp_source.resolve()
    for source, pin in ((atomvm, ATOMVM_PIN), (otp, OTP_PIN)):
        if subprocess.check_output(["git", "-C", source, "rev-parse", "HEAD"], text=True).strip() != pin:
            raise SystemExit(f"Wrong source revision: {source}")
    if subprocess.check_output(["git", "-C", atomvm, "status", "--porcelain"], text=True).strip():
        raise SystemExit("AtomVM source must be clean")
    output = args.output.resolve()
    work = output.parent / (output.stem + ".atomvm-build")
    work.mkdir(parents=True, exist_ok=True)
    host = work / "host"
    run("cmake", "-S", atomvm, "-B", host, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=MinSizeRel",
        "-DAVM_DISABLE_SMP=ON", "-DAVM_DISABLE_TASK_DRIVER=ON", "-DAVM_BUILD_TESTS=OFF")
    run("cmake", "--build", host, "--target", "PackBEAM", "AtomVM", "-j6")
    packbeam = host / "tools/packbeam/PackBEAM"
    if not packbeam.exists():
        packbeam = host / "tools/packbeam/PackBEAM.exe"
    # The source-tree build starts its own pinned emulator, not PATH's OTP 29.
    erl = otp / "bin/erl"
    erlc = otp / "bin/erlc"
    version = subprocess.check_output([erl, "+S", "1", "-noshell", "-eval",
        'io:format("~s", [erlang:system_info(otp_release)]), halt().'], text=True)
    if version != "27" or (otp / "OTP_VERSION").read_text().strip() != "27.3.4.19":
        raise SystemExit("A built OTP 27.3.4.19 host compiler is required")
    beams = work / "beams"
    beams.mkdir(exist_ok=True)
    # Small explicit library closure for io:format/2 and the native console NIF.
    libraries = [atomvm / "libs/estdlib/src" / (name + ".erl")
                 for name in ("io", "io_lib", "lists", "string", "unicode", "proplists")]
    libraries += [atomvm / "libs/eavmlib/src/console.erl", atomvm / "libs/eavmlib/src/atomvm.erl"]
    if args.otp_behaviours:
        libraries += [atomvm / "libs/estdlib/src" / (name + ".erl")
                      for name in ("erlang", "gen_server", "supervisor")]
    run(erlc, "+no_debug_info", "-o", beams, args.application.resolve(), *libraries)
    package = work / "application.avm"
    # PackBEAM refuses an existing output: unlink only this reproducible build
    # product, never a user source or external package.
    if package.exists():
        package.unlink()
    app_beam = beams / (args.application.stem + ".beam")
    run(packbeam, package, app_beam, *[beams / (p.stem + ".beam") for p in libraries])
    run(packbeam, "-l", package)
    data = package.read_bytes()
    generated = work / "generated"
    generated.mkdir(exist_ok=True)
    (generated / "package.h").write_text(
        'static const char package_sha256[] = "' + sha(package) + '";\n'
        + "_Alignas(4) static const unsigned char application_package[] = {\n"
        + ",\n".join(",".join(f"0x{b:02x}" for b in data[i:i+24]) for i in range(0, len(data), 24)) + "\n};\n")
    (generated / "avm_version.h").write_text(
        '#define SYSTEM_NAME "Knytos"\n#define SYSTEM_VERSION "1"\n'
        '#define SYSTEM_ARCHITECTURE "aarch64"\n#define ATOMVM_VERSION "0.6.6"\n')
    core = atomvm / "src/libAtomVM"
    for kind in ("bifs", "nifs"):
        with (generated / (kind + "_hash.h")).open("w") as target:
            run("gperf", "-t", core / (kind + ".gperf"), stdout=target)
    # Reuse the M1 runtime/toolchain and its libc configuration.
    libc_profile = ["--worker-libc"] if args.worker_libc else []
    libc_dir = "picolibc-aarch64-workers-v1" if args.worker_libc else "picolibc-aarch64-v1"
    run("python3", ROOT / "ports/libc/build.py", ROOT / "ports/libc/tests/empty.c",
        "--output", work / "libc-probe.elf", "--picolibc-source", args.picolibc_source, *libc_profile)
    flags = ["--target=aarch64-none-elf", "-std=c11", "-ffreestanding", "-fno-builtin",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-outline-atomics", "-Os",
        "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-Wall", "-Wextra",
        "-DAVM_NO_SMP", "-DAVM_CREATE_STACKTRACES", "-DAVM_VERBOSE_ABORT", "-D_POSIX_C_SOURCE=200809L",
        "-isystem", str(ROOT / "target/c-runtime" / libc_dir / "picolibc/include"),
        "-I", str(core), "-I", str(generated), "-I", str(ROOT / "ports/libc/include")]
    if args.worker_libc:
        flags.append("-D__STDIO_LOCKING")
    # Mirror the exact release's upstream core source list, without POSIX
    # feature probes that could incorrectly succeed for static try-compiles.
    match = re.search(r"set\(SOURCE_FILES\s+(.*?)\)", (core / "CMakeLists.txt").read_text(), re.S)
    sources = [core / name for name in match[1].split()] + [ROOT / "ports/atomvm/sys.c"]
    def compile_source(source):
        obj = work / (source.stem + ".o")
        run("clang", *flags, "-c", source, "-o", obj)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        objects = list(pool.map(compile_source, sources))
    sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], cwd=ROOT, text=True).strip())
    host_triple = subprocess.check_output(["rustc", "-vV"], cwd=ROOT, text=True).split("host: ")[1].splitlines()[0]
    llvm = sysroot / "lib/rustlib" / host_triple / "bin"
    archive = work / "libatomvm.a"
    if archive.exists(): archive.unlink()
    run(llvm / "llvm-ar", "rcs", archive, *objects)
    run("python3", ROOT / "ports/libc/build.py", ROOT / "ports/atomvm/main.c",
        "--output", output, "--picolibc-source", args.picolibc_source,
        "--include", core, "--include", generated, "--define", "AVM_NO_SMP",
        "--library", archive, *libc_profile)
    shutil.copyfile(atomvm / "LICENSE", output.parent / "AtomVM-LICENSE.txt")
    shutil.copyfile(atomvm / "LICENSES/Apache-2.0.txt", output.parent / "AtomVM-Apache-2.0.txt")
    inputs = [ROOT / "ports/atomvm/build.py", ROOT / "ports/atomvm/main.c", args.application.resolve(), *sources, *libraries]
    manifest = {"atomvm_revision": ATOMVM_PIN, "otp_revision": OTP_PIN,
        "otp_version": "27.3.4.19", "package_sha256": sha(package), "package_bytes": len(data),
        "unsigned_elf_sha256": sha(output), "unsigned_elf_bytes": output.stat().st_size,
        "profile": "interpreter-single-scheduler-no-tls-no-posix",
        "worker_libc": args.worker_libc,
        "inputs_sha256": {str(p): sha(p) for p in inputs},
        "core_flags": flags, "host_reference": str(host / "src/AtomVM")}
    output.with_suffix(".atomvm.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Built", output)


if __name__ == "__main__":
    main()
