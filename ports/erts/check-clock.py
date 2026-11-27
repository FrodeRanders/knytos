#!/usr/bin/env python3
"""Check the native monotonic surface against pinned OTP and its error policy."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OTP_PIN = "d199a49a01a2079afb6f2c5c9ae047cd4555e68d"
FUNCTIONS = ("erts_os_monotonic_time", "erts_sys_hrtime")


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
    # Extract the real LP64 branch and two declarations, rather than checking
    # a handwritten copy of OTP's signatures against itself.
    types = re.search(r"#if SIZEOF_LONG == 8\n(.*?)#elif", header, re.S).group(1)
    names = ("ErtsMonotonicTime", "ErtsSysHrTime")
    if types.split() != "typedef long ErtsMonotonicTime; typedef long ErtsSysHrTime;".split():
        raise SystemExit("Unexpected pinned LP64 clock types")
    for name in names:
        types = types.replace(name, "Otp" + name)
    contract = '#include "erts_monotonic.h"\n' + types
    for name in names:
        contract += (f'_Static_assert(__builtin_types_compatible_p({name}, Otp{name}), '
                     f'"OTP type mismatch: {name}");\n')
    for name in FUNCTIONS:
        declarations = re.findall(r"^Erts\w+ " + name + r"\(void\);$", header, re.M)
        if len(declarations) != 1:
            raise SystemExit("Unexpected pinned declaration: " + name)
        declaration = declarations[0].replace(name, "otp_" + name)
        for scalar in names:
            declaration = declaration.replace(scalar, "Otp" + scalar)
        contract += declaration + "\n"
        contract += (f'_Static_assert(__builtin_types_compatible_p(__typeof__({name}), '
                     f'__typeof__(otp_{name})), "OTP signature mismatch: {name}");\n')
    for macro in ("ERTS_COMPILE_TIME_MONOTONIC_TIME_UNIT", "ERTS_HRTIME_UNIT"):
        if not re.search(r"#\s*define " + macro + r" \(1000\*1000\*1000\)", header):
            raise SystemExit("Unexpected pinned nanosecond unit: " + macro)
        contract += f'_Static_assert({macro} == 1000000000, "OTP unit mismatch: {macro}");\n'
    contract += ("static ErtsMonotonicTime (*volatile mono)(void) = erts_os_monotonic_time;\n"
                 "static ErtsSysHrTime (*volatile high)(void) = erts_sys_hrtime;\n"
                 "int main(void) { return mono() > high(); }\n")
    source = out / "contract.c"
    source.write_text(contract)
    archive = out / "libknytos_erts_clock.a"
    run(["python3", ROOT / "ports/erts/build.py", "--monotonic-clock",
         "--picolibc-source", args.picolibc_source, "--output", archive], "archive-build")
    elf = out / "contract.elf"
    run(["python3", ROOT / "ports/libc/build.py", source, "--worker-libc",
         "--include", ROOT / "ports/erts", "--library", archive,
         "--picolibc-source", args.picolibc_source, "--output", elf], "contract-build")
    # The SDK build already verifies the final static AArch64 layout.
    manifest = json.loads(archive.with_suffix(".build.json").read_text())
    sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], cwd=ROOT, text=True).strip())
    rust = subprocess.check_output(["rustc", "-vV"], cwd=ROOT, text=True)
    host_triple = next(line.removeprefix("host: ") for line in rust.splitlines() if line.startswith("host: "))
    nm = sysroot / "lib/rustlib" / host_triple / "bin/llvm-nm"
    symbols = subprocess.check_output([nm, "-g", archive], text=True)
    (out / "archive-symbols.txt").write_text(symbols)
    exports = sorted(re.findall(r"\bT (\w+)$", symbols, re.M))
    imports = sorted(re.findall(r"\bU (\w+)$", symbols, re.M))
    if exports != sorted(FUNCTIONS) or imports != ["abort", "knytos_clock_monotonic_ns", "knytos_console_write"]:
        raise SystemExit("Unexpected native clock archive dependencies")
    flags = manifest["compiler_flags"]
    controls = {}
    for label, old, new in (
        ("signature", "OtpErtsMonotonicTime otp_erts_os_monotonic_time(void);",
         "unsigned long otp_erts_os_monotonic_time(void);"),
        ("unit", "ERTS_HRTIME_UNIT == 1000000000", "ERTS_HRTIME_UNIT == 1000000"),
    ):
        if contract.count(old) != 1:
            raise SystemExit("Unexpected negative control anchor")
        changed = out / ("reject-" + label + ".c")
        changed.write_text(contract.replace(old, new))
        run(["clang", *flags, "-I", ROOT / "ports/erts", "-fsyntax-only", changed],
            "reject-" + label, expected=1)
        diagnostic = "OTP " + label + " mismatch"
        if diagnostic not in (out / ("reject-" + label + ".txt")).read_text():
            raise SystemExit("Negative control failed without expected diagnostic")
        controls[label] = diagnostic

    # Same C implementation, independent host fault source. No native RPC
    # stubs or host-only hooks enter either target object or public header.
    host = out / "host"
    host_command = ["clang", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-fno-builtin",
                    "-I", ROOT / "ports/erts", "-I", ROOT / "ports/libc/include",
                    ROOT / "ports/erts/erts_monotonic.c",
                    ROOT / "ports/erts/tests/monotonic-host.c", "-o", host]
    run(host_command, "host-build")
    cases = []
    for function in range(2):
        for mode in range(14):
            label = f"host-{function}-{mode}"
            status = 42 if mode >= 3 else 0
            run([host, mode, function], label, expected=status)
            cases.append(dict(function=FUNCTIONS[function], mode=mode, status=status))
    inputs = [Path(__file__).resolve(), ROOT / "ports/erts/tests/monotonic-host.c"]
    result = dict(schema="knytos.erts-monotonic-check.v1", otp_revision=OTP_PIN,
                  otp_header_sha256=hashlib.sha256(header.encode()).hexdigest(),
                  functions=list(FUNCTIONS), archive=manifest, elf_sha256=digest(elf),
                  exports=exports, imports=imports,
                  contract_sha256=digest(source), negative_controls=controls,
                  host_command=[str(arg) for arg in host_command], host_cases=cases,
                  inputs_sha256={str(path.relative_to(ROOT)): digest(path) for path in inputs})
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print("Verified two pinned clock interfaces, two rejection controls and 28 host boundary/fault cases")


if __name__ == "__main__":
    main()
