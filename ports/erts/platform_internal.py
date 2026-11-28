"""Build real internal archives and link the common CPU pre-early prefix."""

import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TARGET = "aarch64-unknown-knytos"
SOURCES = ("erl_printf_format.c", "erl_printf.c", "erl_misc_utils.c")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def qualify_internal(out, tree, sdk, flags, toolchain, platform, otp_source, pin):
    work = out / "internal"
    work.mkdir()
    sources = [tree / "erts/lib_src/common" / name for name in SOURCES]
    sources.append(tree / "erts/emulator/beam/erl_cpu_topology.c")
    provenance = {}
    for path in sources:
        relative = str(path.relative_to(tree))
        expected = subprocess.check_output(["git", "-C", otp_source, "rev-parse", pin + ":" + relative], text=True).strip()
        actual = subprocess.check_output(["git", "hash-object", path], text=True).strip()
        if actual != expected:
            raise SystemExit("Internal/common CPU source differs from the pin: " + relative)
        provenance[relative] = {"git_blob": actual, "sha256": digest(path)}
    archives = {name: tree / f"erts/lib/internal/{TARGET}/liberts_internal{suffix}.a"
                for name, suffix in (("regular", ""), ("reentrant", "_r"))}
    command = ["make", "--no-print-directory", "-f", f"{TARGET}/Makefile",
               f"ERL_TOP={tree}", f"TARGET={TARGET}", "TYPE=opt", "V=1",
               *[f"../lib/internal/{TARGET}/{path.name}" for path in archives.values()]]
    with (work / "make.txt").open("w") as stream:
        subprocess.run(command, cwd=tree / "erts/lib_src", stdout=stream, stderr=subprocess.STDOUT, check=True)
    (work / "make-command.json").write_text(json.dumps(command, indent=2) + "\n")
    cpu_obj = tree / f"erts/emulator/obj/{TARGET}/opt/emu/erl_cpu_topology.o"
    cpu_archive = work / "libknytos_erts_cpu_prefix.a"
    subprocess.run([sdk / "llvm-ar", "rcs", cpu_archive, cpu_obj], check=True)
    members = {}
    for name, archive in archives.items():
        actual = subprocess.check_output([sdk / "llvm-ar", "t", archive], text=True).splitlines()
        if actual != [Path(source).with_suffix(".o").name for source in SOURCES]:
            raise SystemExit("Internal archive member selection differs from pinned sources")
        members[name] = actual
    common = platform["common_preflight"]["erl_time_sup.c"]["command"]
    common_flags = common[:common.index("-c")]
    consumer = work / "consumer.c"
    consumer.write_text('#include "config.h"\n#include "sys.h"\n#include "global.h"\n'
                        '#include "erl_cpu_topology.h"\n#include "erl_misc_utils.h"\n#include "erl_printf.h"\n'
                        'static void (*volatile entry)(int*,int*,int*,int*,int*,int*) = erts_pre_early_init_cpu_topology;\n'
                        'static int (*volatile format)(char*,size_t,const char*,...) = erts_snprintf;\n'
                        'int main(void) { return entry == 0 || format == 0; }\n')

    def link(name, source, libraries):
        cmd = [*common_flags, source, "-Wl,--start-group", *libraries, "-Wl,--end-group",
               *shlex.split(toolchain["LDFLAGS"]), *shlex.split(toolchain["LIBS"]), "-o", work / (name + ".elf")]
        with (work / (name + ".txt")).open("w") as stream:
            status = subprocess.run(cmd, cwd=out / "build", stdout=stream, stderr=subprocess.STDOUT).returncode
        return {"status": status, "command": [str(arg) for arg in cmd]}

    spec = importlib.util.spec_from_file_location("internal_elf_check", ROOT / "ports/libc/build.py")
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    links = {}
    for name, archive in archives.items():
        links[name] = link("consumer-" + name, consumer, [cpu_archive, archive])
        if links[name]["status"]:
            raise SystemExit("Actual CPU/formatter internal-library linkage failed: " + name)
        checker.check_elf(work / ("consumer-" + name + ".elf"))
    controls = {}
    for name, libraries, missing in (("cpu", [archives["reentrant"]], "erts_pre_early_init_cpu_topology"),
                                     ("internal", [cpu_archive], "erts_cpu_info_create")):
        controls[name] = link("reject-" + name, consumer, libraries)
        if not controls[name]["status"] or "undefined symbol: " + missing not in (work / ("reject-" + name + ".txt")).read_text():
            raise SystemExit("Internal CPU archive omission did not reject: " + name)
    sleep = work / "reject-sleep.c"
    sleep.write_text('#include "config.h"\n#include "erl_misc_utils.h"\n'
                     'static int (*volatile entry)(long) = erts_milli_sleep;\nint main(void) { return entry == 0; }\n')
    controls["sleep"] = link("reject-sleep", sleep, [archives["reentrant"]])
    if not controls["sleep"]["status"] or "undefined symbol: select" not in (work / "reject-sleep.txt").read_text():
        raise SystemExit("Unported POSIX sleep helper unexpectedly linked")
    stream = work / "reject-stream.c"
    stream.write_text('#include "config.h"\n#include <stdio.h>\n#include "erl_printf.h"\n'
                      'static int (*volatile entry)(const char*,...) = erts_printf;\nint main(void) { return entry == 0; }\n')
    controls["stream"] = link("reject-stream", stream, [archives["reentrant"]])
    if not controls["stream"]["status"] or "undefined symbol: write" not in (work / "reject-stream.txt").read_text():
        raise SystemExit("Unported descriptor stream formatter unexpectedly linked")
    nm = (sdk / "llvm-ar").resolve().with_name("llvm-nm")
    weak_imports = {}
    for name in archives:
        symbols = subprocess.check_output([nm, "-u", work / ("consumer-" + name + ".elf")], text=True)
        (work / ("consumer-" + name + "-undefined.txt")).write_text(symbols)
        # Picolibc's qualified stack-guard constructor probes this optional
        # weak entropy hook. Absence uses its existing non-secret fallback.
        # Every strong or other undefined import must still reject.
        imports = [line.split() for line in symbols.splitlines() if line.strip()]
        if any(parts != ["w", "getentropy"] for parts in imports):
            raise SystemExit("Internal consumer retained an unsupported undefined symbol")
        weak_imports[name] = imports
    result = {"archives": {name: {"path": str(path), "sha256": digest(path), "members": members[name]}
                            for name, path in archives.items()},
              "cpu_archive": str(cpu_archive), "cpu_archive_sha256": digest(cpu_archive),
              "source_provenance": provenance, "make_command": command, "links": links,
              "negative_controls": controls, "cpu_discovery": "unsupported",
              "sdk_weak_imports": weak_imports,
              "inputs_sha256": {str(path.relative_to(ROOT)): digest(path) for path in (
                  ROOT / "ports/erts/tests/internal-cpu.c", ROOT / "ports/libc/build.py")},
              "full_emulator_executed": False}
    (work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result
