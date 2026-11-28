"""Qualify the native platform entry point against actual common early time code."""

import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TARGET = "aarch64-unknown-knytos"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def qualify_pre_init(out, tree, sdk, flags, toolchain, platform):
    work = out / "pre-init"
    work.mkdir()
    obj = tree / f"erts/emulator/obj/{TARGET}/opt/emu/erts_pre_init.o"
    archive = work / "libknytos_erts_pre_init.a"
    subprocess.run([sdk / "llvm-ar", "rcs", archive, obj], check=True)
    nm = (sdk / "llvm-ar").resolve().with_name("llvm-nm")
    symbols = subprocess.check_output([nm, "-g", obj], text=True)
    (work / "symbols.txt").write_text(symbols)
    for name in ("erts_sys_pre_init", "ethr_init", "ethr_fatal_error__", "erts_init_sys_time_sup"):
        if name not in symbols:
            raise SystemExit("Native pre-init did not retain its real entry point/dependency: " + name)
    consumer = work / "consumer.c"
    consumer.write_text('#include "config.h"\n#include "sys.h"\n'
                        'static void (*volatile entry)(void) = erts_sys_pre_init;\n'
                        'int main(void) { return entry == 0; }\n')
    libraries = [archive, out / "time-supervisor/libknytos_erts_calendar.a",
                 out / "platform/libknytos_erts_clock.a", sdk / "libknytos_ethread.a"]

    def link(name, selected):
        command = [*flags, consumer, "-Wl,--start-group", *selected, "-Wl,--end-group",
                   *shlex.split(toolchain["LDFLAGS"]), *shlex.split(toolchain["LIBS"]),
                   "-o", work / (name + ".elf")]
        with (work / (name + ".txt")).open("w") as log:
            result = subprocess.run(command, cwd=out / "build", stdout=log, stderr=subprocess.STDOUT)
        return {"status": result.returncode, "command": [str(arg) for arg in command]}

    positive = link("consumer", libraries)
    if positive["status"]:
        raise SystemExit("Native pre-init actual common-code link failed")
    spec = importlib.util.spec_from_file_location("pre_init_elf_check", ROOT / "ports/libc/build.py")
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    checker.check_elf(work / "consumer.elf")
    controls = {}
    for index, label, missing in ((0, "pre-init", "erts_sys_pre_init"),
                                  (1, "common-time", "erts_init_sys_time_sup"),
                                  (2, "clock", "sys_init_time"), (3, "ethread", "ethr_init")):
        name = "reject-" + label
        control = link(name, libraries[:index] + libraries[index + 1:])
        if not control["status"] or "undefined symbol: " + missing not in (work / (name + ".txt")).read_text():
            raise SystemExit("Native pre-init library omission did not reject: " + label)
        controls[label] = control | {"diagnostic": "undefined symbol: " + missing}
    source = tree / "erts/emulator/sys/knytos/erts_pre_init.c"
    common_command = platform["common_preflight"]["erl_time_sup.c"]["command"]
    common_flags = common_command[:common_command.index("-c")]
    for macro in ("ERTS_ENABLE_LOCK_COUNT", "ERTS_ENABLE_LOCK_CHECK", "ERTS_DYN_LOCK_CHECK",
                  "ERTS_THR_HAVE_SIG_FUNCS"):
        command = [*common_flags, "-D" + macro, "-fsyntax-only", source]
        log = work / ("reject-" + macro + ".txt")
        with log.open("w") as stream:
            status = subprocess.run(command, cwd=out / "build", stdout=stream, stderr=subprocess.STDOUT).returncode
        diagnostic = ("has no Unix signal-mask contract" if macro == "ERTS_THR_HAVE_SIG_FUNCS"
                      else "does not yet support ERTS lock instrumentation")
        if not status or diagnostic not in log.read_text():
            raise SystemExit("Native pre-init unsupported profile did not reject: " + macro)
        controls[macro] = {"status": status, "diagnostic": diagnostic, "command": [str(arg) for arg in command]}
    result = {"archive_sha256": digest(archive), "object_sha256": digest(obj),
              "consumer_sha256": digest(consumer), "consumer_elf_sha256": digest(work / "consumer.elf"),
              "link": positive, "negative_controls": controls,
              "libraries_sha256": {str(path): digest(path) for path in libraries},
              "inputs_sha256": {str(path.relative_to(ROOT)): digest(path) for path in (
                  ROOT / "ports/erts/tests/pre-init.c", ROOT / "ports/erts/ethr_extensions.h")},
              "include_directories": list(dict.fromkeys(common_flags[index + 1]
                  for index, flag in enumerate(common_flags) if flag == "-I")),
              "common_initializer": "erts_init_sys_time_sup", "full_emulator_executed": False}
    (work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result
