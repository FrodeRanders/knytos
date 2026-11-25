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


def replace_pinned(text, old, new):
    if text.count(old) != 1:
        raise SystemExit("Unexpected source for the pinned AtomVM adaptation")
    return text.replace(old, new)


def configure_smp(core, generated, scheduler):
    # The signed roster is fixed. Keep the upstream badarg path for
    # system_flag/2 while retaining SMP synchronization in this translation unit.
    nifs = replace_pinned((core / "nifs.c").read_text(),
        "#ifndef AVM_NO_SMP\n    if (key == SCHEDULERS_ONLINE_ATOM)",
        "#if !defined(AVM_NO_SMP) && !defined(AVM_KNYTOS_SCHEDULERS)\n"
        "    if (key == SCHEDULERS_ONLINE_ATOM)")
    (generated / "nifs.c").write_text(nifs)

    # Reserve the VM's count under its scheduler lock, then wake a prepared
    # thread after releasing the scheduler and ready-list producer locks.
    scheduler = replace_pinned(scheduler,
        "list_remove(&ctx->processes_list_head);\n#ifndef AVM_NO_SMP\n    if (SMP_MUTEX_TRYLOCK",
        "list_remove(&ctx->processes_list_head);\n#ifndef AVM_NO_SMP\n"
        "    bool start_scheduler = false;\n    if (SMP_MUTEX_TRYLOCK")
    scheduler = replace_pinned(scheduler, "            smp_scheduler_start(global);",
        "            start_scheduler = true;")
    scheduler = replace_pinned(scheduler,
        "list_append(&global->ready_processes, &ctx->processes_list_head);\n"
        "    SMP_SPINLOCK_UNLOCK(&global->processes_spinlock);\n#ifndef AVM_NO_SMP\n"
        "    if (SMP_MUTEX_TRYLOCK",
        "list_append(&global->ready_processes, &ctx->processes_list_head);\n"
        "    SMP_SPINLOCK_UNLOCK(&global->processes_spinlock);\n#ifndef AVM_NO_SMP\n"
        "    if (start_scheduler) {\n        smp_scheduler_start(global);\n    }\n"
        "    if (SMP_MUTEX_TRYLOCK")
    scheduler = replace_pinned(scheduler, "        if (!global->waiting_scheduler",
        "        if (!global->scheduler_stop_all && !global->waiting_scheduler")

    # Publish poller availability under the scheduler mutex. Release it
    # before notifying a condition waiter that will reacquire that mutex.
    scheduler = replace_pinned(scheduler,
        "    global->waiting_scheduler = false;\n"
        "    smp_condvar_signal(global->schedulers_cv);\n"
        "    SMP_MUTEX_UNLOCK(global->schedulers_mutex);\n#endif\n\n    return result;",
        "    global->waiting_scheduler = false;\n"
        "    SMP_MUTEX_UNLOCK(global->schedulers_mutex);\n"
        "    smp_condvar_signal(global->schedulers_cv);\n#endif\n\n    return result;")

    # Only the embedded, authenticated pack supplies modules in this profile.
    # Upstream read_priv mutates pin metadata under a shared lock.
    nifs_gperf = (core / "nifs.gperf").read_text()
    for name in ("add_avm_pack_binary/2", "add_avm_pack_file/2", "close_avm_pack/2", "read_priv/2"):
        entry = next(line for line in nifs_gperf.splitlines() if line.startswith(f"atomvm:{name},"))
        nifs_gperf = replace_pinned(nifs_gperf, entry + "\n", "")
    (generated / "nifs.gperf").write_text(nifs_gperf)
    return scheduler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--atomvm-source", required=True, type=Path)
    parser.add_argument("--otp-source", required=True, type=Path)
    parser.add_argument("--picolibc-source", required=True, type=Path)
    parser.add_argument("--worker-libc", action="store_true",
                        help="qualify the worker libc closure with the single-scheduler interpreter")
    parser.add_argument("--service-worker", action="store_true",
                        help="enable signed scalar service calls on a dedicated native worker")
    parser.add_argument("--schedulers", type=int, choices=(1, 2, 4), default=1,
                        help="build a fixed native scheduler roster (2 or 4 implies worker libc)")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--application", type=Path, default=ROOT / "ports/atomvm/tests/hello.erl")
    parser.add_argument("--otp-behaviours", action="store_true",
                        help="include the pinned erlang wrappers, gen_server and supervisor")
    args = parser.parse_args()
    smp = args.schedulers > 1
    if args.service_worker and smp:
        parser.error("The service worker requires a single AtomVM scheduler")
    worker_libc = args.worker_libc or smp or args.service_worker
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
    # A lifecycle request can arrive after the VM has selected a process but
    # before the platform hook returns. Reenter the scheduler's stop path
    # while holding its normal mutex instead of resuming interpreter work.
    scheduler = replace_pinned((core / "scheduler.c").read_text(),
        "    } while (result == NULL);\n\n#ifndef AVM_NO_SMP",
        "    } while (result == NULL || global->scheduler_stop_all);\n\n#ifndef AVM_NO_SMP")
    if smp:
        scheduler = configure_smp(core, generated, scheduler)
    if args.service_worker:
        scheduler = replace_pinned(scheduler, '#include "sys.h"\n',
            '#include "sys.h"\n#include "service_worker.h"\n')
        # Notify after the ready-list guard and before the Context can be
        # freed or publish process monitor notifications.
        scheduler = replace_pinned(scheduler,
            "    SMP_SPINLOCK_UNLOCK(&ctx->global->processes_spinlock);\n    if (!ctx->leader) {",
            "    SMP_SPINLOCK_UNLOCK(&ctx->global->processes_spinlock);\n"
            "    knytos_service_owner_terminated(ctx);\n    if (!ctx->leader) {")
    (generated / "scheduler.c").write_text(scheduler)
    for kind in ("bifs", "nifs"):
        definition = (generated if smp and kind == "nifs" else core) / (kind + ".gperf")
        with (generated / (kind + "_hash.h")).open("w") as target:
            run("gperf", "-t", definition, stdout=target)
    # Reuse the M1 runtime/toolchain and its libc configuration.
    libc_profile = ["--service-worker"] if args.service_worker else ["--worker-libc"] if worker_libc else []
    libc_dir = "picolibc-aarch64-workers-v1" if worker_libc else "picolibc-aarch64-v1"
    run("python3", ROOT / "ports/libc/build.py", ROOT / "ports/libc/tests/empty.c",
        "--output", work / "libc-probe.elf", "--picolibc-source", args.picolibc_source, *libc_profile)
    flags = ["--target=aarch64-none-elf", "-std=c11", "-ffreestanding", "-fno-builtin",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-outline-atomics", "-Os",
        "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-Wall", "-Wextra",
        "-DAVM_CREATE_STACKTRACES", "-DAVM_VERBOSE_ABORT", "-D_POSIX_C_SOURCE=200809L",
        "-isystem", str(ROOT / "target/c-runtime" / libc_dir / "picolibc/include"),
        "-I", str(core), "-I", str(generated), "-I", str(ROOT / "ports/libc/include")]
    defines = ["HAVE_ATOMIC", f"AVM_KNYTOS_SCHEDULERS={args.schedulers}"] if smp else ["AVM_NO_SMP"]
    flags.extend("-D" + define for define in defines)
    if worker_libc:
        flags.append("-D__STDIO_LOCKING")
    if smp:
        flags.append("-DKNYTOS_NATIVE_WORKERS")
    if args.service_worker:
        flags.extend(("-DKNYTOS_NATIVE_WORKERS", "-DKNYTOS_SERVICE_WORKER", "-I", str(ROOT / "ports/atomvm")))
    # Mirror the exact release's upstream core source list, without POSIX
    # feature probes that could incorrectly succeed for static try-compiles.
    match = re.search(r"set\(SOURCE_FILES\s+(.*?)\)", (core / "CMakeLists.txt").read_text(), re.S)
    sources = [core / name for name in match[1].split()] + [ROOT / "ports/atomvm/sys.c"]
    upstream_sources = sources.copy()
    adapted = {"scheduler.c", "nifs.c"} if smp else {"scheduler.c"}
    sources = [generated / path.name if path.name in adapted else path for path in sources]
    if smp:
        sources.append(ROOT / "ports/atomvm/smp.c")
    if args.service_worker:
        sources.append(ROOT / "ports/atomvm/service_worker.c")
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
        "--include", core, "--include", generated,
        *[arg for define in defines for arg in ("--define", define)],
        "--library", archive, *libc_profile)
    shutil.copyfile(atomvm / "LICENSE", output.parent / "AtomVM-LICENSE.txt")
    shutil.copyfile(atomvm / "LICENSES/Apache-2.0.txt", output.parent / "AtomVM-Apache-2.0.txt")
    inputs = [ROOT / "ports/atomvm/build.py", ROOT / "ports/atomvm/main.c", args.application.resolve(),
              *upstream_sources, *sources, *libraries]
    if smp:
        inputs.extend((core / "nifs.gperf", generated / "nifs.gperf"))
    if args.service_worker:
        inputs.append(ROOT / "ports/atomvm/service_worker.h")
    profile = "fixed-smp" if smp else "single-scheduler"
    manifest = {"atomvm_revision": ATOMVM_PIN, "otp_revision": OTP_PIN,
        "otp_version": "27.3.4.19", "package_sha256": sha(package), "package_bytes": len(data),
        "unsigned_elf_sha256": sha(output), "unsigned_elf_bytes": output.stat().st_size,
        "profile": f"interpreter-{profile}-no-tls-no-posix",
                "schedulers": args.schedulers, "worker_libc": worker_libc,
                "service_worker": args.service_worker,
        "inputs_sha256": {str(p): sha(p) for p in inputs},
        "core_flags": flags, "host_reference": str(host / "src/AtomVM")}
    output.with_suffix(".atomvm.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Built", output)


if __name__ == "__main__":
    main()
