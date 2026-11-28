"""Qualify the real common time-supervisor calendar surface and CPU exclusion."""

import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def qualify_time(out, tree, sdk, flags, toolchain, platform):
    work = out / "time-supervisor"
    work.mkdir()
    if platform["common_preflight"]["erl_bif_info.c"]["status"] != 0:
        raise SystemExit("Native time requires the whole common BIF source to compile")
    obj = out / "metadata/erl_time_sup.o"
    archive = work / "libknytos_erts_calendar.a"
    subprocess.run([sdk / "llvm-ar", "rcs", archive, obj], check=True)
    nm = (sdk / "llvm-ar").resolve().with_name("llvm-nm")
    symbols = subprocess.check_output([nm, "-g", obj], text=True)
    (work / "symbols.txt").write_text(symbols)
    if any(name in symbols for name in ("erts_runtime_elapsed_both", "getrusage", "sys_times", " mktime", " localtime", " time\n")):
        raise SystemExit("Native time supervisor retained a CPU/POSIX calendar fallback")
    # Preprocess the entire actual BIF source with the same real headers and
    # configuration. This does not execute a fabricated Erlang Process.
    command = platform["common_preflight"]["erl_bif_info.c"]["command"]
    index = command.index("-c")
    preprocessed = work / "erl_bif_info.i"
    with preprocessed.open("w") as stream, (work / "bif-preprocess.txt").open("w") as log:
        subprocess.run([*command[:index], "-E", "-P", "-CC", command[index + 1]],
                       cwd=out / "build", stdout=stream, stderr=log, check=True)
    text = preprocessed.read_text()
    marker = "No generation-preserving user/kernel CPU-time contract yet."
    if text.count(marker) != 1 or "erts_runtime_elapsed_both" in text:
        raise SystemExit("Native runtime-statistic branch did not exclude the unsupported CPU reader")
    excerpt = text[text.index(marker):text.index(marker) + 1200]
    if "freason" not in excerpt:
        raise SystemExit("Native runtime-statistic branch did not select a BIF exception")
    (work / "bif-excerpt.txt").write_text(excerpt)
    # Independent host error/UTC tests for the adapter. Real common calendar
    # execution is qualified separately by the signed native fixture.
    host = work / "calendar-host"
    host_command = ["clang", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-fno-builtin",
                    "-DKNYTOS_NATIVE_WORKERS", "-Dgmtime_r=knytos_test_gmtime_r",
                    "-I", ROOT / "ports/erts", "-I", ROOT / "ports/libc/include",
                    ROOT / "ports/erts/erts_calendar.c", ROOT / "ports/erts/tests/calendar-host.c",
                    "-o", host]
    with (work / "host-build.txt").open("w") as log:
        subprocess.run(host_command, stdout=log, stderr=subprocess.STDOUT, check=True)
    for mode in range(8):
        with (work / f"host-{mode}.txt").open("w") as log:
            status = subprocess.run([host, str(mode)], stdout=log, stderr=subprocess.STDOUT).returncode
        if status != (42 if mode == 6 else 0):
            raise SystemExit(f"Calendar host case {mode} failed: {status}")
    result = {"archive_sha256": digest(archive), "source_object_sha256": digest(obj),
              "whole_bif_compile_status": platform["common_preflight"]["erl_bif_info.c"]["status"],
              "bif_preprocessed_sha256": digest(preprocessed), "cpu_statistics_unsupported": True,
              "host_cases": 8, "host_command": [str(arg) for arg in host_command],
              "inputs_sha256": {str(path.relative_to(ROOT)): digest(path) for path in (
                  ROOT / "ports/erts/tests/calendar-host.c", ROOT / "ports/erts/tests/calendar-values.h")},
              "whole_time_supervisor_executed": False}
    (work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result
