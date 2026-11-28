"""Stage and check the explicit bounded native clock configure SDK."""

import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FUNCTIONS = (
    "sys_init_time", "erts_late_sys_init_time", "knytos_erts_init_time",
    "erts_os_monotonic_time", "erts_sys_hrtime", "erts_os_system_time", "erts_os_times",
    "knytos_erts_clock_publish", "knytos_erts_clock_refresh", "knytos_erts_clock_read",
    "erts_sys_perf_counter", "erts_sys_perf_counter_unit",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(source, picolibc, out, tree, sdk, age_ms):
    archive = out / "libknytos_erts_clock.a"
    with (out / "clock-build.txt").open("w") as log:
        subprocess.run(["python3", ROOT / "ports/erts/build.py", "--platform-clock",
                        "--otp-source", source, "--picolibc-source", picolibc,
                        "--clock-sample-age-ms", str(age_ms), "--output", archive],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    manifest = json.loads(archive.with_suffix(".build.json").read_text())
    shutil.copy2(archive, sdk)
    headers = sdk / "clock"
    headers.mkdir()
    for name in ("erts_monotonic.h", "erts_system_time.h", "erts_clock_platform.h", "erts_time_init.h"):
        shutil.copy2(ROOT / "ports/erts" / name, headers)
    shutil.copy2(archive.with_suffix(".build") / "time-contract/erts_time_contract.h", headers)
    (headers / "clock-policy.h").write_text(
        f"#define KNYTOS_ERTS_SDK_CLOCK_MAX_AGE_MS {age_ms}\n")
    contract = '''/* Explicit pinned time-record probe, not the complete native sys.h. */
#include "erts_time_init.h"
#include "clock-policy.h"
#ifndef KNYTOS_SERVICE_WORKER
#error "Native clock SDK requires the service-worker profile"
#endif
_Static_assert(ERTS_KNYTOS_CLOCK_MAX_AGE_MS == KNYTOS_ERTS_SDK_CLOCK_MAX_AGE_MS,
               "Native clock SDK sample-age policy mismatch");
_Static_assert(sizeof(ErtsMonotonicTime) == 8 && sizeof(ErtsSystemTime) == 8,
               "Native clock SDK requires signed LP64 nanoseconds");
'''
    # Volatile pointers are actually read by the Autoconf main. They keep every
    # interface (including inline perf-counter wrappers) alive under section GC.
    signatures = (
        "void (*volatile ref_sys_init_time)(ErtsSysInitTimeResult *)",
        "void (*volatile ref_erts_late_sys_init_time)(void)",
        "int (*volatile ref_knytos_erts_init_time)(ErtsSysInitTimeResult *, uint64_t)",
        "ErtsMonotonicTime (*volatile ref_erts_os_monotonic_time)(void)",
        "ErtsSysHrTime (*volatile ref_erts_sys_hrtime)(void)",
        "ErtsSystemTime (*volatile ref_erts_os_system_time)(void)",
        "void (*volatile ref_erts_os_times)(ErtsMonotonicTime *, ErtsSystemTime *)",
        "int (*volatile ref_knytos_erts_clock_publish)(const struct knytos_realtime_sample *, uint64_t)",
        "int (*volatile ref_knytos_erts_clock_refresh)(uint64_t)",
        "int (*volatile ref_knytos_erts_clock_read)(ErtsSystemTime *)",
        "ErtsSysPerfCounter (*volatile ref_erts_sys_perf_counter)(void)",
        "ErtsSysPerfCounter (*volatile ref_erts_sys_perf_counter_unit)(void)",
    )
    for name, signature in zip(FUNCTIONS, signatures, strict=True):
        contract += f"static {signature} = {name};\n"
        contract += (f'_Static_assert(__builtin_types_compatible_p(__typeof__({name}), '
                     f'__typeof__(*ref_{name})), "Native clock signature mismatch: {name}");\n')
    (headers / "clock-link-check.h").write_text(contract)
    shutil.copy2(ROOT / "ports/erts/knytos-clock.m4", tree / "make/autoconf/knytos-clock.m4")
    subprocess.run(["patch", "-p1", "--batch", "--fuzz=0", "--input",
                    ROOT / "ports/erts/otp-29.1.2-clock-configure.patch"], cwd=tree, check=True)
    return {"archive": manifest, "retained_functions": list(FUNCTIONS),
            "sdk_sha256": {path.name: digest(path) for path in sorted(headers.iterdir())}}


def controls(out, sdk, toolchain, age_ms):
    """Check the actual configure ELF and execute the actual macro's rejects."""
    from importlib.util import module_from_spec, spec_from_file_location
    source = out / "build/knytos-clock-link.c"
    if not source.exists():
        raise SystemExit("Native configure never passed its clock link check")
    elf = source.with_suffix(".elf")
    spec = spec_from_file_location("knytos_clock_elf_check", ROOT / "ports/libc/build.py")
    checker = module_from_spec(spec)
    spec.loader.exec_module(checker)
    checker.check_elf(elf)
    symbols = subprocess.check_output([(sdk / "llvm-ar").resolve().with_name("llvm-nm"),
                                       "--defined-only", elf], text=True)
    refs = re.findall(r"\bref_(\w+)\s*$", symbols, re.M)
    if sorted(refs) != sorted(FUNCTIONS):
        raise SystemExit("Clock link ELF did not retain all twelve clock interfaces")
    flags = [*shlex.split(toolchain["CC"]), *shlex.split(toolchain["CFLAGS"]),
             *shlex.split(toolchain["CPPFLAGS"]), "-I../sdk/clock",
             "-DKNYTOS_ERTS_TIME_CONTRACT_PROBE"]
    checks = {}
    for name, extra, link, diagnostic in (
        ("archive-omitted", [f"-DERTS_KNYTOS_CLOCK_MAX_AGE_MS={age_ms}"], True,
         "undefined symbol: sys_init_time"),
        ("policy-mismatch", [f"-DERTS_KNYTOS_CLOCK_MAX_AGE_MS={age_ms + 1}"], False,
         "Native clock SDK sample-age policy mismatch"),
        ("profile-omitted", [f"-DERTS_KNYTOS_CLOCK_MAX_AGE_MS={age_ms}",
                             "-UKNYTOS_SERVICE_WORKER"], False,
         "Native clock SDK requires the service-worker profile"),
    ):
        command = [*flags, *extra, source]
        command += ([*shlex.split(toolchain["LDFLAGS"]), *shlex.split(toolchain["LIBS"]),
                     "-o", out / (name + ".elf")] if link else ["-fsyntax-only"])
        log = out / ("reject-clock-" + name + ".txt")
        with log.open("w") as stream:
            result = subprocess.run(command, cwd=out / "build", stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode == 0 or diagnostic not in log.read_text():
            raise SystemExit(f"Clock rejection control failed: {name}")
        checks[name] = {"status": result.returncode, "diagnostic": diagnostic}
    # Change an independently expected signature while retaining the actual
    # native declarations. This must fail even without -Werror.
    header = (sdk / "clock/clock-link-check.h").read_text()
    anchor = "ErtsSystemTime (*volatile ref_erts_os_system_time)(void)"
    if header.count(anchor) != 1:
        raise SystemExit("Unexpected clock signature control anchor")
    bad = out / "reject-clock-signature.c"
    bad.write_text(header.replace(anchor, "unsigned long (*volatile ref_erts_os_system_time)(void)"))
    with (out / "reject-clock-signature.txt").open("w") as stream:
        result = subprocess.run([*flags, f"-DERTS_KNYTOS_CLOCK_MAX_AGE_MS={age_ms}",
                                 "-fsyntax-only", bad], cwd=out / "build",
                                stdout=stream, stderr=subprocess.STDOUT)
    diagnostic = "Native clock signature mismatch: erts_os_system_time"
    if result.returncode == 0 or diagnostic not in (out / "reject-clock-signature.txt").read_text():
        raise SystemExit("Clock signature control did not reject")
    checks["signature"] = {"status": result.returncode, "diagnostic": diagnostic}
    return {"elf_layout_validated": True, "retained_reference_count": len(refs),
            "source_sha256": digest(source), "elf_sha256": digest(elf), "controls": checks}


def macro_controls(out, toolchain, age_ms):
    """Exercise the production m4 branch independently of later OTP gates."""
    directory = out / "clock-macro"
    directory.mkdir()
    for name in ("config.sub", "config.guess"):
        shutil.copy2(out / "otp/make/autoconf" / name, directory)
    shutil.copy2(ROOT / "ports/erts/knytos-clock.m4", directory)
    (directory / "configure.ac").write_text('''AC_INIT([knytos-clock-contract], [1])
AC_CONFIG_SRCDIR([knytos-clock.m4])
m4_include([knytos-clock.m4])
AC_CANONICAL_HOST
AC_PROG_CC
AC_ARG_VAR([ETHR_THR_LIB_BASE], [selected thread backend])
AC_ARG_WITH([knytos-clock])
AC_ARG_WITH([knytos-clock-max-age-ms])
AC_ARG_WITH([clock-resolution])
AC_ARG_WITH([clock-gettime-realtime-id])
AC_ARG_WITH([clock-gettime-monotonic-id])
AC_ARG_ENABLE([gettimeofday-as-os-system-time])
AC_ARG_ENABLE([prefer-elapsed-monotonic-time-during-suspend])
ERL_KNYTOS_CLOCK
AC_CONFIG_HEADERS([selected-clock.h])
AC_CONFIG_FILES([selected-libs])
AC_OUTPUT
''')
    (directory / "selected-clock.h.in").write_text('''#undef ERTS_KNYTOS_CLOCK
#undef ERTS_KNYTOS_CLOCK_MAX_AGE_NS
#undef HAVE_MONOTONIC_ERTS_SYS_HRTIME
#undef ERTS_HAVE_CORRECTED_OS_MONOTONIC_TIME
#undef OS_SYSTEM_TIME_GETTIMEOFDAY
#undef OS_SYSTEM_TIME_USING_CLOCK_GETTIME
#undef OS_MONOTONIC_TIME_USING_CLOCK_GETTIME
#undef HAVE_CLOCK_GETTIME_CPU_TIME
''')
    (directory / "selected-libs.in").write_text("LIBRT=@LIBRT@\n")
    with (directory / "autoconf.txt").open("w") as stream:
        subprocess.run(["autoconf"], cwd=directory, stdout=stream, stderr=subprocess.STDOUT, check=True)
    build = subprocess.check_output([directory / "config.guess"], text=True).strip()
    env = {name: value for name, value in os.environ.items()
           if not name.startswith(("ac_cv_", "erl_cv_", "erl_xcomp_"))}
    env |= toolchain | {"ETHR_THR_LIB_BASE": "knytos"}
    cases = (
        ("selected", [], {}, None),
        ("target", ["--host=x86_64-unknown-knytos"], {}, "requires aarch64-unknown-knytos"),
        ("backend", [], {"ETHR_THR_LIB_BASE": "pthread"}, "requires native Knytos ethread"),
        ("missing-age", ["--with-knytos-clock-max-age-ms="], {}, "bounded nonzero Knytos clock sample age"),
        ("zero-age", ["--with-knytos-clock-max-age-ms=0"], {}, "sample age must be 1..2147483647"),
        ("invalid-age", ["--with-knytos-clock-max-age-ms=abc"], {}, "bounded nonzero Knytos clock sample age"),
        ("large-age", ["--with-knytos-clock-max-age-ms=2147483648"], {}, "sample age must be 1..2147483647"),
        ("overflow-age", ["--with-knytos-clock-max-age-ms=999999999999999999999999999999"], {},
         "sample age must be 1..2147483647"),
        ("leading-zero-age", ["--with-knytos-clock-max-age-ms=05000"], {},
         "sample age must be 1..2147483647"),
        ("posix", ["--with-clock-gettime-realtime-id=CLOCK_REALTIME"], {}, "cannot select POSIX clock options"),
    )
    results = {}
    for name, extra, overrides, diagnostic in cases:
        work = out / ("clock-macro-" + name)
        work.mkdir()
        command = [directory / "configure", f"--srcdir={directory}",
                   f"--build={build}", "--host=aarch64-unknown-knytos",
                   "--with-knytos-clock=../sdk", f"--with-knytos-clock-max-age-ms={age_ms}", *extra]
        with (work / "output.txt").open("w") as stream:
            result = subprocess.run(command, cwd=work, env=env | overrides,
                                    stdout=stream, stderr=subprocess.STDOUT)
        if diagnostic:
            if result.returncode == 0 or diagnostic not in (work / "output.txt").read_text():
                raise SystemExit("Clock macro rejection failed: " + name)
        else:
            if result.returncode != 0:
                raise SystemExit("Independent clock macro selection failed")
            config = (work / "selected-clock.h").read_text()
            required = ("#define ERTS_KNYTOS_CLOCK 1", "#define HAVE_MONOTONIC_ERTS_SYS_HRTIME 1",
                        f"#define ERTS_KNYTOS_CLOCK_MAX_AGE_NS ({age_ms}*1000000ULL)")
            if not all(value in config for value in required):
                raise SystemExit("Native clock definitions were not emitted")
            if (re.search(r"^#define (?:OS_|ERTS_HAVE_CORRECTED|HAVE_CLOCK_GETTIME_CPU)", config, re.M)
                    or (work / "selected-libs").read_text().strip() != "LIBRT="):
                raise SystemExit("Native selection claimed foreign clocks or CPU time")
        results[name] = {"status": result.returncode, "diagnostic": diagnostic}
    return results


def configured_outputs(out, age_ms):
    """Inspect actual AC_OUTPUT products, without claiming a working Make graph."""
    target = "aarch64-unknown-knytos"
    root = out / "build"
    config_path = root / target / "config.h"
    config = config_path.read_text()
    required = ("#define ETHR_KNYTOS 1", "#define ERTS_KNYTOS_CLOCK 1",
                "#define HAVE_MONOTONIC_ERTS_SYS_HRTIME 1",
                f"#define ERTS_KNYTOS_CLOCK_MAX_AGE_NS ({age_ms}*1000000ULL)")
    if not all(line in config for line in required):
        raise SystemExit("Configured OTP headers did not select native clocks and threads")
    if re.search(r"^#define (?:OS_SYSTEM_TIME_\w*|OS_MONOTONIC_TIME_\w*|SYS_HRTIME_USING_\w*|"
                 r"ERTS_HAVE_CORRECTED_OS_MONOTONIC_TIME|HAVE_CLOCK_GETTIME_CPU_TIME|"
                 r"HAVE_GETHRVTIME_PROCFS_IOCTL)\b", config, re.M):
        raise SystemExit("Configured OTP headers claimed a foreign clock or CPU-time backend")
    lib_make = root / "lib_src" / target / "Makefile"
    if "ETHR_THR_LIB_BASE_DIR=knytos" not in lib_make.read_text():
        raise SystemExit("Configured ethread Makefile did not select native sources")
    files = (config_path, lib_make, root / "emulator" / target / "Makefile",
             root / "include/internal" / target / "ethread_header_config.h",
             root / "include/internal" / target / "ethread.mk")
    return {"sha256": {str(path.relative_to(root)): digest(path) for path in files},
            "full_emulator_build_qualified": False,
            "remaining_platform": "common sys.h and emulator Make still select Unix platform sources"}
