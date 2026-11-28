"""Stage the native sys header and qualify generated emulator clock rules."""

import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TARGET = "aarch64-unknown-knytos"
CLOCK_SOURCES = ("erts_monotonic.c", "erts_system_time.c", "erts_time_init.c")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(tree, metadata=False, native_time=False):
    platform = tree / "erts/emulator/sys/knytos"
    platform.mkdir()
    for name in (*CLOCK_SOURCES, "erl_knytos_sys.h", "erts_monotonic.h", "erts_system_time.h",
                 "erts_clock_platform.h", "erts_time_init.h"):
        shutil.copy2(ROOT / "ports/erts" / name, platform)
    subprocess.run(["patch", "-p1", "--batch", "--fuzz=0", "--input",
                    ROOT / "ports/erts/otp-29.1.2-platform.patch"], cwd=tree, check=True)
    if metadata:
        subprocess.run(["patch", "-p1", "--batch", "--fuzz=0", "--input",
                        ROOT / "ports/erts/otp-29.1.2-metadata.patch"], cwd=tree, check=True)
    if native_time:
        for name in ("erts_calendar.c", "erts_calendar.h"):
            shutil.copy2(ROOT / "ports/erts" / name, platform)
        subprocess.run(["patch", "-p1", "--batch", "--fuzz=0", "--input",
                        ROOT / "ports/erts/otp-29.1.2-time-supervisor.patch"], cwd=tree, check=True)


def qualify(out, tree, sdk, toolchain, age_ms, generate_metadata=False, otp_source=None, native_time=False):
    work = out / "platform"
    work.mkdir()
    build = out / "build"
    # Relocate AC_OUTPUT products to OTP's native in-tree Make layout. No
    # replacement common header, generated BEAM code or platform stub is used.
    mappings = {
        build / TARGET: tree / "erts" / TARGET,
        build / "include" / TARGET: tree / "erts/include" / TARGET,
        build / "include/internal" / TARGET: tree / "erts/include/internal" / TARGET,
        build / "emulator" / TARGET: tree / "erts/emulator" / TARGET,
        out / "make" / TARGET: tree / "make" / TARGET,
    }
    staged = {}
    for source, destination in mappings.items():
        shutil.copytree(source, destination, dirs_exist_ok=True)
        for path in source.rglob("*"):
            if path.is_file():
                staged[str((destination / path.relative_to(source)).relative_to(tree))] = digest(path)
    # The top-level configure normally renders this logging-only template.
    # Verbosity is explicit; no target feature/configuration is synthesized.
    output = (tree / "make/output.mk.in").read_text()
    if output.count("@DEFAULT_VERBOSITY@") != 1:
        raise SystemExit("Unexpected OTP output template")
    (tree / "make/output.mk").write_text(output.replace("@DEFAULT_VERBOSITY@", "1"))
    generated = f"{TARGET}/opt/emu/GENERATED"
    command = ["make", "--no-print-directory", "-f", f"{TARGET}/Makefile",
               f"ERL_TOP={tree}", f"TARGET={TARGET}", "TYPE=opt", "FLAVOR=emu", "V=1",
               "ERTS_SKIP_DEPEND=true", "-o", generated,
               "-o", f"{TARGET}/gen_git_version.mk", "knytos-clock-objects"]
    metadata = None
    if generate_metadata:
        from platform_metadata import generate
        metadata = generate(out, tree, otp_source)
        index = command.index(generated)
        del command[index - 1:index + 1]
    with (work / "make.txt").open("w") as log:
        result = subprocess.run(command, cwd=tree / "erts/emulator",
                                stdout=log, stderr=subprocess.STDOUT)
    (work / "make-command.json").write_text(json.dumps(command, indent=2) + "\n")
    if result.returncode:
        raise SystemExit(f"Native clock Make build failed; see {work / 'make.txt'}")
    sources = (*CLOCK_SOURCES, "erts_calendar.c") if native_time else CLOCK_SOURCES
    objects = [tree / "erts/emulator/obj" / TARGET / "opt/emu" / Path(name).with_suffix(".o")
               for name in sources]
    archive = work / "libknytos_erts_clock.a"
    subprocess.run([sdk / "llvm-ar", "rcs", archive, *objects], check=True)
    # Use the actual generated Makefile for selection inspection too. The
    # standalone clocks do not consume generated BEAM code or the whole-VM
    # dependency scan; -o names those exclusions without fabricating files.
    inspection = work / "inspect.mk"
    inspection.write_text(".PHONY: knytos-inspect\nknytos-inspect:\n"
                          "\t@echo $(ERLANG_OSTYPE)\n\t@echo $(OPSYS)\n"
                          "\t@echo $(OS_OBJS)\n\t@echo drivers=$(DRV_OBJS)\n")
    inspect = command[:-1] + ["-f", inspection, "knytos-inspect"]
    selection = subprocess.check_output(inspect, cwd=tree / "erts/emulator", text=True)
    (work / "selection.txt").write_text(selection)
    rows = selection.splitlines()
    expected = [str(path.relative_to(tree / "erts/emulator")) for path in objects]
    if rows[:2] != ["knytos", "knytos"] or rows[2].split() != expected or rows[3] != "drivers=":
        raise SystemExit("Generated Make did not select exactly the implemented native platform sources")
    includes = [tree / "erts" / TARGET, tree / "erts/emulator/beam", tree / "erts/emulator/sys/knytos",
                tree / "erts/emulator/sys/common", tree / "erts/include", tree / "erts/include" / TARGET,
                tree / "erts/include/internal", tree / "erts/include/internal" / TARGET, sdk / "clock"]
    flags = [*shlex.split(toolchain["CC"]), "-std=gnu23", *shlex.split(toolchain["CFLAGS"]),
             *shlex.split(toolchain["CPPFLAGS"]), "-Wall", "-Werror", "-DHAVE_CONFIG_H", "-DUSE_THREADS"]
    for include in includes:
        flags.extend(("-I", str(include)))
    original = (sdk / "clock/erts_time_contract.h").read_text()
    record = original.split("typedef struct {", 1)[1].split("} ErtsSysInitTimeResult;", 1)[0]
    contract = ('#include "config.h"\n#include "sys.h"\n#include "erts_time_init.h"\n'
                '#include "clock-link-check.h"\n'
                '#if defined(UNIX) || defined(_ERL_UNIX_SYS_H) || defined(KNYTOS_ERTS_TIME_CONTRACT_PROBE)\n'
                '#error "Native consumer selected a substitute sys or time record"\n#endif\n'
                'typedef struct {' + record + '} ExpectedTimeResult;\n')
    spec = importlib.util.spec_from_file_location("erts_pinned_time_check", ROOT / "ports/erts/check-time-init.py")
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    for property in ("sizeof", "_Alignof"):
        contract += (f'_Static_assert({property}(ErtsSysInitTimeResult) == {property}(ExpectedTimeResult), '
                     '"Native time record layout mismatch");\n')
    for field in checker.FIELDS:
        contract += (f'_Static_assert(offsetof(ErtsSysInitTimeResult, {field}) == '
                     f'offsetof(ExpectedTimeResult, {field}), "Native time record offset mismatch");\n'
                     f'_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->{field}), '
                     f'__typeof__(((ExpectedTimeResult *)0)->{field})), "Native time record field mismatch");\n')
    from configure_clock import FUNCTIONS
    contract += "int main(void) { return " + " || ".join(f"ref_{name} == 0" for name in FUNCTIONS) + "; }\n"
    consumer = work / "consumer.c"
    consumer.write_text(contract)
    elf = work / "consumer.elf"
    with (work / "consumer.txt").open("w") as log:
        subprocess.run([*flags, f"-DERTS_KNYTOS_CLOCK_MAX_AGE_MS={age_ms}", "-MD", "-MF",
                        work / "consumer.d", consumer, archive, *shlex.split(toolchain["LDFLAGS"]),
                        *shlex.split(toolchain["LIBS"]), "-o", elf],
                       cwd=out / "build", stdout=log, stderr=subprocess.STDOUT, check=True)
    spec = importlib.util.spec_from_file_location("erts_platform_elf_check", ROOT / "ports/libc/build.py")
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    checker.check_elf(elf)
    dependencies = (work / "consumer.d").read_text().replace("\\\n", " ").split(":", 1)[1].split()
    required = [tree / "erts/emulator/beam/sys.h", tree / "erts/emulator/beam/erl_threads.h",
                tree / "erts/emulator/sys/knytos/erl_knytos_sys.h", tree / "erts/include/internal/ethread.h"]
    if not all(str(path) in dependencies for path in required) or any(
            Path(path).name in ("erl_unix_sys.h", "erl_win_sys.h", "erts_time_contract.h") for path in dependencies):
        raise SystemExit("Native consumer did not include complete real common/native/thread headers")
    controls = {}
    for name, extra, diagnostic in (
        ("unix", ["-DUNIX=1"], "cannot select another platform"),
        ("profile", ["-UKNYTOS_SERVICE_WORKER"], "requires the service-worker C profile"),
        ("probe-record", ["-DKNYTOS_ERTS_TIME_CONTRACT_PROBE"], "requires the complete common time record"),
        ("descriptor", ["-DWANT_NONBLOCKING"], "descriptor readiness is not implemented"),
    ):
        log = work / ("reject-" + name + ".txt")
        with log.open("w") as stream:
            result = subprocess.run([*flags, f"-DERTS_KNYTOS_CLOCK_MAX_AGE_MS={age_ms}", *extra,
                                     "-fsyntax-only", consumer], cwd=out / "build",
                                    stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode == 0 or diagnostic not in log.read_text():
            raise SystemExit("Native platform rejection failed: " + name)
        controls[name] = {"status": result.returncode, "diagnostic": diagnostic}
    bad = work / "reject-field.c"
    expected_record = "typedef struct {" + record + "} ExpectedTimeResult;"
    bad.write_text(contract.replace(expected_record, expected_record.replace(
        "ErtsMonotonicTime sys_clock_resolution;", "int sys_clock_resolution;")))
    with (work / "reject-field.txt").open("w") as log:
        result = subprocess.run([*flags, f"-DERTS_KNYTOS_CLOCK_MAX_AGE_MS={age_ms}",
                                 "-fsyntax-only", bad], cwd=out / "build",
                                stdout=log, stderr=subprocess.STDOUT)
    if result.returncode == 0 or "Native time record field mismatch" not in (work / "reject-field.txt").read_text():
        raise SystemExit("Native time record field control did not reject")
    controls["field"] = {"status": result.returncode, "diagnostic": "Native time record field mismatch"}
    members = {path.name: digest(path) for path in objects}
    result = {"schema": "knytos.erts-native-platform.v1", "make_command": command,
              "staged_outputs_sha256": staged, "archive_sha256": digest(archive),
              "members_sha256": members, "consumer_sha256": digest(consumer), "elf_sha256": digest(elf),
              "compiler_flags": flags, "include_directories": [str(path) for path in includes],
              "dependencies": dependencies, "negative_controls": controls,
              "make_version": subprocess.check_output(["make", "--version"], text=True).splitlines()[0],
              "full_emulator_qualified": False}
    if metadata:
        from platform_metadata import common_preflight
        result["generated_metadata"] = metadata
        result["common_preflight"] = common_preflight(out, tree, flags, native_time=native_time)
    if native_time:
        from platform_time import qualify_time
        result["native_time"] = qualify_time(out, tree, sdk, flags, toolchain, result)
    (work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result
