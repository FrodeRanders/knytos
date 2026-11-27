#!/usr/bin/env python3
"""Qualify the standalone published UTC clock surface against pinned OTP."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OTP_PIN = "d199a49a01a2079afb6f2c5c9ae047cd4555e68d"
FUNCTIONS = ("erts_os_monotonic_time", "erts_sys_hrtime", "erts_os_system_time", "erts_os_times")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--otp-source", type=Path, required=True)
    parser.add_argument("--picolibc-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)

    def run(arguments, label, expected=0):
        with (out / (label + ".txt")).open("w") as log:
            result = subprocess.run([str(arg) for arg in arguments], cwd=ROOT,
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode != expected:
            raise SystemExit(f"{label}: unexpected exit {result.returncode}; see {out}")

    header = subprocess.check_output(["git", "-C", args.otp_source, "show",
                                      f"{OTP_PIN}:erts/emulator/sys/unix/erl_unix_sys.h"], text=True)
    types = re.search(r"#if SIZEOF_LONG == 8\n(.*?)#elif", header, re.S).group(1)
    alias = "typedef ErtsMonotonicTime ErtsSystemTime;"
    if header.count(alias) != 1:
        raise SystemExit("Unexpected pinned system-time type")
    types += alias + "\n"
    names = ("ErtsMonotonicTime", "ErtsSysHrTime", "ErtsSystemTime")
    for name in names:
        types = types.replace(name, "Otp" + name)
    contract = '#include "erts_system_time.h"\n' + types
    for name in names:
        contract += (f'_Static_assert(__builtin_types_compatible_p({name}, Otp{name}), '
                     f'"OTP type mismatch: {name}");\n')
    for name in FUNCTIONS:
        declarations = re.findall(r"^(?:Erts\w+|void) " + name + r"\([^\n]*\);$", header, re.M)
        if len(declarations) != 1:
            raise SystemExit("Unexpected pinned declaration: " + name)
        declaration = declarations[0].replace(name, "otp_" + name)
        for scalar in names:
            declaration = declaration.replace(scalar, "Otp" + scalar)
        contract += declaration + "\n"
        contract += (f'_Static_assert(__builtin_types_compatible_p(__typeof__({name}), '
                     f'__typeof__(otp_{name})), "OTP signature mismatch: {name}");\n')
    contract += '''
static ErtsMonotonicTime (*volatile mono)(void) = erts_os_monotonic_time;
static ErtsSysHrTime (*volatile high)(void) = erts_sys_hrtime;
static ErtsSystemTime (*volatile system_time)(void) = erts_os_system_time;
static void (*volatile paired)(ErtsMonotonicTime *, ErtsSystemTime *) = erts_os_times;
int main(void) {
    ErtsMonotonicTime m;
    ErtsSystemTime s = system_time();
    struct knytos_realtime_sample sample = {0};
    paired(&m, &s);
    return knytos_erts_clock_refresh(5000000000ULL)
        + knytos_erts_clock_publish(&sample, 5000000000ULL)
        + knytos_erts_clock_read(&s) + (mono() > high());
}
'''
    source = out / "contract.c"
    source.write_text(contract)
    archive = out / "libknytos_erts_clock.a"
    run(["python3", ROOT / "ports/erts/build.py", "--system-clock",
         "--picolibc-source", args.picolibc_source, "--output", archive], "archive-build")
    elf = out / "contract.elf"
    run(["python3", ROOT / "ports/libc/build.py", source, "--service-worker",
         "--include", ROOT / "ports/erts", "--library", archive,
         "--picolibc-source", args.picolibc_source, "--output", elf], "contract-build")
    manifest = json.loads(archive.with_suffix(".build.json").read_text())
    sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], cwd=ROOT, text=True).strip())
    rust = subprocess.check_output(["rustc", "-vV"], cwd=ROOT, text=True)
    host_triple = next(line.removeprefix("host: ") for line in rust.splitlines() if line.startswith("host: "))
    nm = sysroot / "lib/rustlib" / host_triple / "bin/llvm-nm"
    symbols = subprocess.check_output([nm, "-g", archive], text=True)
    (out / "archive-symbols.txt").write_text(symbols)
    exports = sorted(re.findall(r"\bT (\w+)$", symbols, re.M))
    imports = sorted(set(re.findall(r"\bU (\w+)$", symbols, re.M)) - set(exports))
    private = ["knytos_erts_clock_publish", "knytos_erts_clock_read", "knytos_erts_clock_refresh"]
    if exports != sorted([*FUNCTIONS, *private]) or imports != [
        "abort", "knytos_clock_monotonic_ns", "knytos_clock_realtime_sample",
        "knytos_clock_sample_project_ns", "knytos_console_write",
    ]:
        raise SystemExit("Unexpected clock archive surface or dependencies")
    controls = {}
    for label, old, new in (
        ("system-return", "OtpErtsSystemTime otp_erts_os_system_time(void);",
         "unsigned long otp_erts_os_system_time(void);"),
        ("paired-pointer", "otp_erts_os_times(OtpErtsMonotonicTime *, OtpErtsSystemTime *);",
         "otp_erts_os_times(unsigned long *, OtpErtsSystemTime *);"),
    ):
        if contract.count(old) != 1:
            raise SystemExit("Unexpected rejection control anchor")
        changed = out / ("reject-" + label + ".c")
        changed.write_text(contract.replace(old, new))
        run(["clang", *manifest["compiler_flags"], "-I", ROOT / "ports/erts", "-fsyntax-only", changed],
            "reject-" + label, expected=1)
        if "OTP signature mismatch" not in (out / ("reject-" + label + ".txt")).read_text():
            raise SystemExit("Rejection lacked signature diagnostic")
        controls[label] = "OTP signature mismatch"

    base = ["clang", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
            "-pthread", "-DKNYTOS_NATIVE_WORKERS", "-I", ROOT / "ports/erts",
            "-I", ROOT / "ports/libc/include", ROOT / "ports/erts/tests/system-time-host.c",
            ROOT / "ports/erts/erts_monotonic.c"]
    host_commands = []
    for sanitizer in ("undefined", "thread"):
        host = out / ("host-" + sanitizer)
        command = [*base, "-fsanitize=" + sanitizer, "-fno-sanitize-recover=all", "-o", host]
        run(command, "host-" + sanitizer + "-build")
        host_commands.append([str(arg) for arg in command])
        for mode in (range(9) if sanitizer == "undefined" else (7, 8)):
            run([host, mode], f"host-{sanitizer}-{mode}", expected=42 if mode in (0, 6) else 0)
    result = dict(schema="knytos.erts-system-clock-check.v1", otp_revision=OTP_PIN,
                  otp_header_sha256=hashlib.sha256(header.encode()).hexdigest(),
                  functions=list(FUNCTIONS), archive=manifest, exports=exports, imports=imports,
                  elf_sha256=digest(elf), contract_sha256=digest(source), negative_controls=controls,
                  host_cases=9, thread_sanitizer_stress_passed=True, host_commands=host_commands,
                  inputs_sha256={str(path.relative_to(ROOT)): digest(path) for path in (
                      Path(__file__).resolve(), ROOT / "ports/erts/tests/system-time-host.c")})
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print("Verified four pinned clock interfaces, two rejection controls, nine host cases and TSan stress/reuse")


if __name__ == "__main__":
    main()
