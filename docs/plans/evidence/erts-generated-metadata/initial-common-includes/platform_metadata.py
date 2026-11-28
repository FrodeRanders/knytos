"""Generate real pinned ERTS metadata using a pinned-source host YCF tool."""

import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TARGET = "aarch64-unknown-knytos"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_host_inputs(tree, manifest):
    for name, expected in manifest.items():
        content = (tree / name).read_bytes()
        actual = hashlib.sha1(b"blob " + str(len(content)).encode() + b"\0" + content).hexdigest()
        if actual != expected:
            raise ValueError("Pinned host YCF source mismatch: " + name)


def generate(out, tree, otp_source):
    work = out / "metadata"
    work.mkdir()
    host = tree / "erts/lib_src/yielding_c_fun"
    from configure import OTP_PIN
    entries = subprocess.check_output(["git", "-C", otp_source, "ls-tree", "-r", "-z",
                                       OTP_PIN, "--", "erts/lib_src/yielding_c_fun"])
    manifest = {}
    for entry in entries.split(b"\0"):
        if not entry:
            continue
        metadata, name = entry.decode().split("\t")
        relative = Path(name).relative_to("erts/lib_src/yielding_c_fun")
        source_file = len(relative.parts) == 1 and relative.suffix in (".c", ".h")
        test_file = relative.parent == Path("test/examples") and relative.suffix in (".c", ".h", ".out")
        if not (source_file or test_file or str(relative) in (
                "Makefile", "bin/yielding_c_fun", "test/test.sh")):
            continue
        mode, kind, object_id = metadata.split()
        if kind != "blob" or mode not in ("100644", "100755"):
            raise SystemExit("Unexpected pinned host generator object")
        manifest[name] = object_id
    if len(manifest) < 50:
        raise SystemExit("Incomplete pinned host generator source manifest")
    verify_host_inputs(tree, manifest)
    (work / "host-source-objects.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # Change one copied source in an isolated control, before invoking a host
    # compiler or generator. The actual exported generator is never modified.
    sample = "erts/lib_src/yielding_c_fun/ycf_main.c"
    control = work / "source-control"
    (control / sample).parent.mkdir(parents=True)
    (control / sample).write_bytes((tree / sample).read_bytes() + b"\n/* changed source */\n")
    try:
        verify_host_inputs(control, {sample: manifest[sample]})
    except ValueError as error:
        (work / "reject-host-source.txt").write_text(str(error) + "\n")
    else:
        raise SystemExit("Changed host generator source was not rejected")
    command = ["make", "--no-print-directory", "-f", "Makefile", "CC=clang", "CFLAGS=-O2", "-j4"]
    with (work / "host-build.txt").open("w") as log:
        subprocess.run(command, cwd=host, stdout=log, stderr=subprocess.STDOUT, check=True)
    executable = host / "bin/yielding_c_fun.bin"
    host_env = os.environ | {"PATH": str(host / "bin") + os.pathsep + os.environ.get("PATH", "")}
    with (work / "host-tests.txt").open("w") as log:
        subprocess.run(["bash", "test/test.sh"], cwd=host, stdout=log, stderr=subprocess.STDOUT,
                       env=host_env, check=True, timeout=300)
    make = ["make", "--no-print-directory", "-f", f"{TARGET}/Makefile", f"ERL_TOP={tree}",
            f"TARGET={TARGET}", "TYPE=opt", "FLAVOR=emu", "V=1", "ERTS_SKIP_DEPEND=true",
            "-o", f"{TARGET}/gen_git_version.mk", f"YCF_EXECUTABLE_PATH={executable}"]
    target = f"{TARGET}/opt/emu/GENERATED"
    env = os.environ | {"PERL_HASH_SEED": "0", "PERL_PERTURB_KEYS": "0"}
    with (work / "generate.txt").open("w") as log:
        subprocess.run([*make, target], cwd=tree / "erts/emulator", env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
    # Inspect the actual complete generated target and every prerequisite.
    inspect = work / "inspect.mk"
    inspect.write_text(".PHONY: knytos-generated-inputs\nknytos-generated-inputs:\n"
                       "\t@echo $(GENERATE)\n")
    inputs = subprocess.check_output([*make, "-f", inspect, "knytos-generated-inputs"],
                                     cwd=tree / "erts/emulator", env=env, text=True).split()
    expected = {f"{TARGET}/opt/emu/" + name for name in (
        "OPCODES-GENERATED", "TABLES-GENERATED", "erl_alloc_types.h", "driver_tab.c",
        "erl_db_insert_list.ycf.h", "erl_map.ycf.h", "utils.ycf.h")}
    expected.add(f"{TARGET}/erl_version.h")
    if set(inputs) != expected or not (tree / "erts/emulator" / target).is_file():
        raise SystemExit("Unexpected native ERTS generated target or prerequisites")
    outputs = [tree / "erts/emulator" / path for path in inputs]
    directory = tree / "erts/emulator" / TARGET / "opt/emu"
    outputs.extend(directory / name for name in (
        "beam_cold.h", "beam_warm.h", "beam_hot.h", "beam_opcodes.c", "beam_opcodes.h",
        "erl_bif_table.c", "erl_bif_table.h", "erl_bif_list.h", "erl_atom_table.c", "erl_atom_table.h",
        "erl_guard_bifs.c", "erl_dirty_bif_wrap.c"))
    outputs.append(tree / "erts/emulator" / target)
    if any(not path.is_file() or not path.stat().st_size for path in outputs):
        raise SystemExit("Missing or empty generated ERTS metadata")
    opcodes = (directory / "beam_opcodes.h").read_text()
    if '#error "small code model assumed' in opcodes or "@CODE_MODEL@" in (work / "generate.txt").read_text():
        raise SystemExit("Native interpreter opcode generation assumed compressed code pointers")
    alloc = (directory / "erl_alloc_types.h").read_text()
    if "#define ERTS_ALC_A_MIN (1)" not in alloc:
        raise SystemExit("Generated allocator metadata is missing its real allocator enumeration")
    native_files = {str(path.relative_to(tree)): digest(path) for path in outputs}
    # Force regeneration using Make's -W without changing the source or its
    # timestamp. OTP's empty .SECONDARY may otherwise classify absent tools as
    # intermediates and leave an already-generated header up to date.
    missing = work / "missing-ycf"
    rejection = [*(arg for arg in make if not arg.startswith("YCF_EXECUTABLE_PATH=")),
                 f"YCF_EXECUTABLE_PATH={missing}", "-W", "beam/erl_map.c",
                 f"{TARGET}/opt/emu/erl_map.ycf.h"]
    with (work / "reject-missing-ycf.txt").open("w") as log:
        rejected = subprocess.run(rejection, cwd=tree / "erts/emulator", env=env,
                                  stdout=log, stderr=subprocess.STDOUT)
    if rejected.returncode == 0 or str(missing) not in (work / "reject-missing-ycf.txt").read_text():
        raise SystemExit("Missing pinned host generator did not reject")
    if any(digest(tree / name) != expected for name, expected in native_files.items()):
        raise SystemExit("Generator rejection changed completed metadata")
    host_inputs = [host / "Makefile", host / "test/test.sh", *sorted(host.glob("*.c")),
                   *sorted(host.glob("*.h")), *sorted((host / "test/examples").glob("*.c")),
                   *sorted((host / "test/examples").glob("*.out"))]
    result = {"schema": "knytos.erts-generated-metadata.v1", "host_build_command": command,
              "host_clang": subprocess.check_output(["clang", "--version"], text=True).strip(),
              "host_ycf_sha256": digest(executable), "host_tests_passed": True,
              "host_git_objects": manifest, "changed_host_source_rejected": True,
              "missing_host_ycf_rejected": True,
              "host_inputs_sha256": {str(path.relative_to(tree)): digest(path) for path in host_inputs},
              "make_arguments": make, "generated_target": target,
              "generated_sha256": native_files, "generated_prerequisites": inputs}
    (work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def common_preflight(out, tree, flags):
    """Compile unchanged common sources and retain actual native platform gates."""
    work = out / "metadata"
    directory = tree / "erts/emulator" / TARGET / "opt/emu"
    results = {}
    for name in ("erl_term.c", "erl_time_sup.c", "erl_init.c"):
        source = tree / "erts/emulator/beam" / name
        command = [*flags, "-I", str(directory), "-I", str(tree / "erts/emulator" / TARGET),
                   "-ferror-limit=12", "-c", source,
                   "-o", work / Path(name).with_suffix(".o")]
        log = work / (name + ".txt")
        with log.open("w") as stream:
            completed = subprocess.run(command, cwd=out / "build", stdout=stream, stderr=subprocess.STDOUT)
        results[name] = {"status": completed.returncode, "source_sha256": digest(source),
                         "diagnostic_sha256": digest(log), "command": [str(arg) for arg in command]}
        if completed.returncode == 0:
            results[name]["object_sha256"] = digest(work / Path(name).with_suffix(".o"))
    if results["erl_term.c"]["status"] != 0:
        raise SystemExit("Unchanged common erl_term.c did not compile with genuine metadata")
    log = work / "reject-metadata-omitted.txt"
    with log.open("w") as stream:
        completed = subprocess.run([*flags, "-I", str(tree / "erts/emulator" / TARGET),
                                    "-fsyntax-only", tree / "erts/emulator/beam/erl_term.c"],
                                   cwd=out / "build", stdout=stream, stderr=subprocess.STDOUT)
    if completed.returncode == 0 or "erl_alloc_types.h" not in log.read_text():
        raise SystemExit("Omitting generated metadata did not reject common term compilation")
    results["generation_omitted_control"] = {"status": completed.returncode,
                                               "diagnostic": "erl_alloc_types.h not found"}
    (work / "common-preflight.json").write_text(json.dumps(results, indent=2) + "\n")
    return results
