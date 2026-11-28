#!/usr/bin/env python3
"""Probe the pinned ERTS configure script against the AArch64 Knytos C SDK."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
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


def prepare_sdk(picolibc, out, native_clock=False):
    control = out / "sdk-control.elf"
    subprocess.run(["python3", ROOT / "ports/libc/build.py", ROOT / "ports/libc/tests/empty.c",
                    "--service-worker" if native_clock else "--worker-libc",
                    "--picolibc-source", picolibc, "--output", control],
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


def prepare_native(source, picolibc, out, tree, sdk):
    """Stage the qualified backend and its full retained-reference link probe."""
    native = out / "native"
    with (out / "native-interface.txt").open("w") as log:
        subprocess.run(["python3", ROOT / "ports/erts/check-interface.py", "--upstream-header",
                        "--otp-source", source, "--picolibc-source", picolibc,
                        "--output", native], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    interface = json.loads((native / "result.json").read_text())
    include = native / "otp/erts/include/internal"
    shutil.copytree(include, sdk / "include/internal")
    shutil.copy2(native / "libknytos_ethread.a", sdk)
    contract = (native / "contract.c").read_text()
    anchor = "\nint main(void) {\n"
    if contract.count(anchor) != 1:
        raise SystemExit("Unexpected retained-reference consumer")
    (sdk / "ethread-link-check.h").write_text(contract.split(anchor)[0] + "\n")
    # Use the same selected public headers in the exported OTP tree.
    shutil.copy2(include / "ethread.h", tree / "erts/include/internal/ethread.h")
    shutil.copytree(include / "knytos", tree / "erts/include/internal/knytos")
    from build import SOURCES
    backend = tree / "erts/lib_src/knytos"
    backend.mkdir()
    for name in (*SOURCES, *(path.name for path in sorted((ROOT / "ports/erts").glob("*.h")))):
        shutil.copy2(ROOT / "ports/erts" / name, backend / name)
    shutil.copy2(ROOT / "ports/erts/knytos-ethread.m4", tree / "make/autoconf/knytos-ethread.m4")
    patch = ROOT / "ports/erts/otp-29.1.2-native-configure.patch"
    subprocess.run(["patch", "-p1", "--batch", "--fuzz=0", "--input", patch], cwd=tree, check=True)
    # Check the actual Make conditional without pretending a complete OTP
    # Makefile was generated before configure reaches AC_OUTPUT.
    def selected_sources(text, mode, label):
        start = "ETHR_THR_LIB_BASE_DIR=@ETHR_THR_LIB_BASE_DIR@"
        end = "ifneq ($(strip $(CREATE_DIRS)),)"
        if text.count(start) != 1 or text.count(end) != 1:
            raise SystemExit("Unexpected ethread Makefile boundaries")
        fragment = start + text.split(start)[1].split(end)[0]
        fragment = fragment.replace("@ETHR_THR_LIB_BASE_DIR@", mode).replace("@ETHR_X86_SSE2_ASM@", "no")
        makefile = out / (label + ".mk")
        makefile.write_text("ETHR_LIB_NAME=ethread\nUSING_VC=no\n" + fragment +
                            "\n.PHONY: sources\nsources:\n\t@echo $(ETHREAD_LIB_SRC)\n")
        return subprocess.check_output(["make", "--no-print-directory", "-f", makefile, "sources"],
                                       cwd=tree / "erts/lib_src", text=True).split()
    patched = (tree / "erts/lib_src/Makefile.in").read_text()
    original = capture("git", "-C", source, "show", f"{OTP_PIN}:erts/lib_src/Makefile.in")
    native_sources = selected_sources(patched, "knytos", "native-sources")
    if native_sources != ["knytos/" + name for name in SOURCES]:
        raise SystemExit("Native Make source list does not match the qualified archive")
    pthread_sources = selected_sources(patched, "pthread", "pthread-sources")
    if pthread_sources != selected_sources(original, "pthread", "original-pthread-sources"):
        raise SystemExit("Native selection changed the pthread Make source list")
    staged = out / "native-source-build"
    staged.mkdir()
    objects = []
    with (out / "native-source-build.txt").open("w") as log:
        for name in SOURCES:
            obj = staged / Path(name).with_suffix(".o")
            subprocess.run(["clang", *interface["archive"]["compiler_flags"], "-DETHR_KNYTOS",
                            "-c", backend / name, "-o", obj], cwd=ROOT,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            objects.append(obj)
    staged_archive = out / "libstaged_ethread.a"
    subprocess.run([sdk / "llvm-ar", "rcs", staged_archive, *objects], check=True)
    if hashlib.sha256(staged_archive.read_bytes()).hexdigest() != interface["archive"]["sha256"]:
        raise SystemExit("Staged native sources did not reproduce the qualified archive")
    (out / "native-sources.json").write_text(json.dumps({
        "native": native_sources, "pthread": pthread_sources,
        "staged_archive_sha256": interface["archive"]["sha256"],
    }, indent=2) + "\n")
    return interface


def native_link_controls(out, sdk, toolchain, interface):
    """Reject a missing archive and conflicting backend using the real probe."""
    source = out / "build/knytos-ethread-link.c"
    if not source.exists():
        return None, None  # The genuine configure check itself did not pass.
    elf = source.with_suffix(".elf")
    spec = importlib.util.spec_from_file_location("knytos_libc_build", ROOT / "ports/libc/build.py")
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    checker.check_elf(elf)
    nm = (sdk / "llvm-ar").resolve().with_name("llvm-nm")
    symbols = subprocess.check_output([nm, "--defined-only", elf], text=True)
    references = re.findall(r"\bref_(ethr_\w+)\s*$", symbols, re.M)
    expected = interface["functions"] + interface["native_callable_names"]
    if len(references) != len(expected) or set(references) != set(expected):
        raise SystemExit("Autoconf ELF did not retain the complete native interface")
    flags = [*shlex.split(toolchain["CC"]), *shlex.split(toolchain["CFLAGS"]),
             *shlex.split(toolchain["CPPFLAGS"]), "-I../sdk/include/internal", "-I../sdk"]
    controls = {}
    for name, command, expected in (
        ("archive-omitted", [*flags, source, *shlex.split(toolchain["LDFLAGS"]),
                             *shlex.split(toolchain["LIBS"]), "-o", out / "archive-omitted.elf"],
         "undefined symbol: ethr_"),
        ("backend-conflict", [*flags, "-DETHR_PTHREADS=1", "-fsyntax-only", source],
         "Knytos ethread cannot select another thread backend"),
    ):
        log = out / ("reject-" + name + ".txt")
        with log.open("w") as stream:
            result = subprocess.run(command, cwd=out / "build", stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode == 0 or expected not in log.read_text():
            raise SystemExit(f"Native configure rejection control failed: {name}")
        controls[name] = {"status": result.returncode, "diagnostic": expected}
    return controls, {"elf_layout_validated": True, "retained_reference_count": len(references)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--otp-source", type=Path, required=True)
    parser.add_argument("--picolibc-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--native-ethread", action="store_true",
                        help="select and genuinely compile/link the bounded native thread backend")
    parser.add_argument("--native-clock", action="store_true",
                        help="also compile/link the bounded native clock SDK (not a complete sys layer)")
    parser.add_argument("--native-platform", action="store_true",
                        help="select the complete common sys.h and native clock Make routing")
    parser.add_argument("--native-metadata", action="store_true",
                        help="also build pinned host YCF and generate real upstream emulator metadata")
    parser.add_argument("--native-time", action="store_true",
                        help="also adapt common time-supervisor UTC calendars and reject CPU statistics")
    parser.add_argument("--native-pre-init", action="store_true",
                        help="also build and link native platform early thread/clock initialization")
    parser.add_argument("--native-internal", action="store_true",
                        help="also build internal libraries and qualify common CPU pre-initialization")
    parser.add_argument("--clock-sample-age-ms", type=int,
                        help="explicit finite clock sample-age policy for --native-clock")
    args = parser.parse_args()
    if args.native_internal and not args.native_pre_init:
        parser.error("--native-internal requires --native-pre-init")
    if args.native_pre_init and not args.native_time:
        parser.error("--native-pre-init requires --native-time")
    if args.native_time and not args.native_metadata:
        parser.error("--native-time requires --native-metadata")
    if args.native_metadata and not args.native_platform:
        parser.error("--native-metadata requires --native-platform")
    if args.native_platform and not args.native_clock:
        parser.error("--native-platform requires --native-clock and --native-ethread")
    if args.native_clock and not args.native_ethread:
        parser.error("--native-clock requires --native-ethread")
    if args.native_clock:
        if args.clock_sample_age_ms is None or not 1 <= args.clock_sample_age_ms <= 2147483647:
            parser.error("--native-clock requires --clock-sample-age-ms=1..2147483647")
    elif args.clock_sample_age_ms is not None:
        parser.error("--clock-sample-age-ms requires --native-clock")
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
    sdk, control_manifest = prepare_sdk(args.picolibc_source.resolve(), out, args.native_clock)
    interface = prepare_native(source, args.picolibc_source.resolve(), out, tree, sdk) if args.native_ethread else None
    clock = None
    if args.native_clock:
        from configure_clock import prepare
        clock = prepare(source, args.picolibc_source.resolve(), out, tree, sdk, args.clock_sample_age_ms)
    if args.native_platform:
        from platform_build import prepare
        prepare(tree, metadata=args.native_metadata, native_time=args.native_time,
                native_pre_init=args.native_pre_init, native_internal=args.native_internal)
    if args.native_ethread:
        with (out / "autoconf.txt").open("w") as log:
            subprocess.run(["autoconf", "-I", "../make/autoconf", "-o", "configure", "configure.ac"],
                           cwd=tree / "erts", stdout=log, stderr=subprocess.STDOUT, check=True)
    build_target = capture(tree / "make/autoconf/config.guess")
    command = [tree / "erts/configure", f"--srcdir={tree / 'erts'}", f"--build={build_target}",
               f"--host={TARGET}", "--disable-jit"]
    if args.native_ethread:
        command.extend(("--with-knytos-ethread=../sdk", "--without-termcap"))
    if args.native_clock:
        command.extend(("--with-knytos-clock=../sdk",
                        f"--with-knytos-clock-max-age-ms={args.clock_sample_age_ms}"))
    if args.native_platform:
        command.append("--enable-knytos-platform")
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
        "CFLAGS": "-Os -ffreestanding -fno-builtin -fno-pic -fno-pie -mno-outline-atomics",
        "CPPFLAGS": "-Dmain=knytos_c_main -DKNYTOS_NATIVE_WORKERS -D__STDIO_LOCKING "
                    "-DKNYTOS_WORKER_LIBC -isystem ../sdk/include",
        "CXXFLAGS": "-Os -ffreestanding -fno-pic -fno-pie -mno-outline-atomics",
        "LDFLAGS": "--ld-path=../sdk/ld.lld -nostdlib -Wl,-T,../sdk/link.x,--entry=_start,--gc-sections",
        "LIBS": "-Wl,--start-group ../sdk/bridge.o ../sdk/libknytos_cabi.a ../sdk/libc.a -Wl,--end-group",
    }
    if args.native_clock:
        toolchain["CPPFLAGS"] += " -DKNYTOS_SERVICE_WORKER"
    if args.native_platform:
        # Generated Make rules run below the configure directory. The SDK must
        # retain its location there too. GNU visibility exposes implemented
        # libc functions such as memrchr, which configure genuinely links.
        toolchain = {name: value.replace("../sdk", str(sdk)) for name, value in toolchain.items()}
        toolchain["CPPFLAGS"] += " -D_GNU_SOURCE"
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
        "native_interface": interface,
        "toolchain": toolchain,
        "inputs_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in ([Path(__file__).resolve()] + ([ROOT / "ports/erts/knytos-ethread.m4",
                              ROOT / "ports/erts/otp-29.1.2-native-configure.patch"] if args.native_ethread else []))},
    }
    # Preserve configure's own outcome even if a later qualification control
    # fails. The final write below adds the successful control results.
    (out / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
    if args.native_ethread:
        evidence["autoconf"] = capture("autoconf", "--version").splitlines()[0]
        evidence["generated_configure_sha256"] = hashlib.sha256((tree / "erts/configure").read_bytes()).hexdigest()
        evidence["native_sdk_sha256"] = {name: hashlib.sha256((sdk / name).read_bytes()).hexdigest()
                                        for name in ("libknytos_ethread.a", "ethread-link-check.h")}
        evidence["native_sources"] = json.loads((out / "native-sources.json").read_text())
        evidence["native_link_controls"], evidence["native_link_check"] = native_link_controls(out, sdk, toolchain, interface)
        for name in ("knytos-ethread-link.c", "knytos-ethread-link.elf"):
            path = build / name
            if path.exists():
                evidence.setdefault("native_link_sha256", {})[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    if args.native_clock:
        from configure_clock import controls, macro_controls, configured_outputs
        evidence["native_clock"] = clock
        if (build / "knytos-clock-link.c").exists():
            evidence["native_clock_link"] = controls(out, sdk, toolchain, args.clock_sample_age_ms)
            evidence["native_clock_macro_controls"] = macro_controls(out, toolchain, args.clock_sample_age_ms)
        elif result.returncode == 0:
            raise SystemExit("Configure succeeded without passing its native clock link check")
        if result.returncode == 0:
            evidence["configured_outputs"] = configured_outputs(out, args.clock_sample_age_ms)
        evidence["inputs_sha256"].update({str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                         for path in (ROOT / "ports/erts/configure_clock.py",
                                                      ROOT / "ports/erts/knytos-clock.m4",
                                                      ROOT / "ports/erts/otp-29.1.2-clock-configure.patch")})
    if args.native_platform and result.returncode == 0:
        from platform_build import qualify
        evidence["native_platform"] = qualify(out, tree, sdk, toolchain, args.clock_sample_age_ms,
                                              generate_metadata=args.native_metadata, otp_source=source,
                                              native_time=args.native_time, native_pre_init=args.native_pre_init,
                                              native_internal=args.native_internal, otp_pin=OTP_PIN)
        evidence["inputs_sha256"].update({str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                         for path in (ROOT / "ports/erts/platform_build.py",
                                                      ROOT / "ports/erts/otp-29.1.2-platform.patch",
                                                      ROOT / "ports/erts/erl_knytos_sys.h",
                                                      ROOT / "ports/erts/erts_time_init.h")})
        if args.native_metadata:
            evidence["inputs_sha256"].update({str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                             for path in (ROOT / "ports/erts/platform_metadata.py",
                                                          ROOT / "ports/erts/otp-29.1.2-metadata.patch")})
        if args.native_time:
            evidence["inputs_sha256"].update({str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                             for path in (ROOT / "ports/erts/erts_calendar.c",
                                                          ROOT / "ports/erts/erts_calendar.h",
                                                          ROOT / "ports/erts/platform_time.py",
                                                          ROOT / "ports/erts/otp-29.1.2-time-supervisor.patch")})
        if args.native_pre_init:
            evidence["inputs_sha256"].update({str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                             for path in (ROOT / "ports/erts/erts_pre_init.c",
                                                          ROOT / "ports/erts/platform_pre_init.py",
                                                          ROOT / "ports/erts/otp-29.1.2-pre-init.patch")})
        if args.native_internal:
            evidence["inputs_sha256"].update({str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                             for path in (ROOT / "ports/erts/platform_internal.py",
                                                          ROOT / "ports/erts/otp-29.1.2-internal-cpu.patch")})
    (out / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"ERTS configure exited {result.returncode}. See {log}")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
