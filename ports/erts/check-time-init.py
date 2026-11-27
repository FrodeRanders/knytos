#!/usr/bin/env python3
"""Check the pinned ERTS startup time record, adapter and failure policy."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FIELDS = ("have_os_monotonic_time", "have_corrected_os_monotonic_time",
          "os_monotonic_time_unit", "sys_clock_resolution",
          *("os_monotonic_time_info." + name for name in
            ("resolution", "used_resolution", "func", "clock_id", "locked_use", "extended")),
          *("os_system_time_info." + name for name in ("resolution", "func", "clock_id", "locked_use")))


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

    def run(command, label, expected=0):
        with (out / (label + ".txt")).open("w") as log:
            result = subprocess.run([str(arg) for arg in command], cwd=ROOT,
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode != expected:
            raise SystemExit(f"{label}: unexpected exit {result.returncode}; see {out}")

    archive = out / "libknytos_erts_clock.a"
    run(["python3", ROOT / "ports/erts/build.py", "--platform-clock", "--otp-source", args.otp_source,
         "--picolibc-source", args.picolibc_source, "--output", archive], "archive-build")
    manifest = json.loads(archive.with_suffix(".build.json").read_text())
    directory = archive.with_suffix(".build") / "time-contract"
    header = directory / "erts_time_contract.h"
    original = header.read_text()
    record = original.split("typedef struct {", 1)[1].split("} ErtsSysInitTimeResult;", 1)[0]
    contract = ('#include <stddef.h>\n#include "erts_time_init.h"\n'
                'typedef struct {' + record + '} ExpectedTimeResult;\n')
    for property in ("sizeof", "_Alignof"):
        contract += (f'_Static_assert({property}(ErtsSysInitTimeResult) == {property}(ExpectedTimeResult), '
                     '"OTP time layout mismatch");\n')
    for field in FIELDS:
        contract += (f'_Static_assert(offsetof(ErtsSysInitTimeResult, {field}) == '
                     f'offsetof(ExpectedTimeResult, {field}), "OTP time offset mismatch");\n'
                     f'_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->{field}), '
                     f'__typeof__(((ExpectedTimeResult *)0)->{field})), "OTP time field mismatch");\n')
    contract += '''
_Static_assert(__builtin_types_compatible_p(__typeof__(&sys_init_time), void (*)(ErtsSysInitTimeResult *)), "init signature");
_Static_assert(__builtin_types_compatible_p(__typeof__(&erts_late_sys_init_time), void (*)(void)), "late signature");
static void (*volatile init_time)(ErtsSysInitTimeResult *) = sys_init_time;
static void (*volatile late_time)(void) = erts_late_sys_init_time;
int main(void) {
    ErtsSysInitTimeResult result;
    init_time(&result);
    late_time();
    return knytos_erts_init_time(&result, 5000000000ULL)
        + (erts_sys_perf_counter() < 0) + (erts_sys_perf_counter_unit() != 1000000000);
}
'''
    source = out / "contract.c"
    source.write_text(contract)
    elf = out / "contract.elf"
    run(["python3", ROOT / "ports/libc/build.py", source, "--service-worker",
         "--include", ROOT / "ports/erts", "--include", directory,
         "--define", "KNYTOS_ERTS_TIME_CONTRACT_PROBE", "--library", archive,
         "--picolibc-source", args.picolibc_source, "--output", elf], "contract-build")
    bad = out / "reject-field.c"
    # Only the independently expected record is changed; the real declarations
    # remain the exact record extracted from pinned sys.h.
    expected = 'typedef struct {' + record + '} ExpectedTimeResult;'
    bad.write_text(contract.replace(expected, expected.replace('ErtsMonotonicTime sys_clock_resolution;',
                                                               'int sys_clock_resolution;')))
    run(["clang", *manifest["compiler_flags"], "-fsyntax-only", bad], "reject-field", expected=1)
    assert "OTP time field mismatch" in (out / "reject-field.txt").read_text()
    flags = [flag for flag in manifest["compiler_flags"] if not flag.startswith("-DERTS_KNYTOS_CLOCK_MAX_AGE_NS=")]
    for name, extra in (("missing-policy", []), ("zero-policy", ["-DERTS_KNYTOS_CLOCK_MAX_AGE_NS=0"])):
        run(["clang", *flags, *extra, "-fsyntax-only", ROOT / "ports/erts/erts_time_init.c"],
            "reject-" + name, expected=1)
        assert "sample-age policy" in (out / ("reject-" + name + ".txt")).read_text()
    host = out / "host"
    host_command = ["clang", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-DKNYTOS_NATIVE_WORKERS",
                    "-DKNYTOS_ERTS_TIME_CONTRACT_PROBE", "-DERTS_KNYTOS_CLOCK_MAX_AGE_NS=5000000000ULL",
                    "-I", ROOT / "ports/erts", "-I", ROOT / "ports/libc/include", "-I", directory,
                    ROOT / "ports/erts/erts_time_init.c", ROOT / "ports/erts/erts_monotonic.c",
                    ROOT / "ports/erts/tests/time-init-host.c", "-o", host]
    run(host_command, "host-build")
    for mode in range(9):
        run([host, mode], "host-" + str(mode), expected=42 if mode == 6 else 0)
    result = dict(schema="knytos.erts-time-init-check.v1", archive=manifest,
                  contract_sha256=digest(source), elf_sha256=digest(elf),
                  record_fields=list(FIELDS), negative_controls=["field", "missing-policy", "zero-policy"],
                  host_cases=9, host_command=[str(arg) for arg in host_command],
                  inputs_sha256={str(path.relative_to(ROOT)): digest(path) for path in (
                      Path(__file__).resolve(), ROOT / "ports/erts/tests/time-init-host.c")})
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print("Verified pinned initialization record, 14 field layouts/types, three rejection controls and nine host cases")


if __name__ == "__main__":
    main()
