#!/usr/bin/env python3
"""Compile and link standalone adapters against pinned OTP declaration slices."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from configure import OTP_PIN
from stage_headers import stage_headers

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM_HEADERS = (
    "erts/include/internal/ethread.h",
    "erts/include/internal/ethr_atomics.h",
    "erts/include/internal/ethr_mutex.h",
    "erts/include/internal/pthread/ethr_event.h",
    "erts/include/internal/gcc/ethr_membar.h",
)
PORTABLE_STRUCTS = (
    "ethr_init_data", "ethr_thr_opts", "ethr_memory_allocator",
    "ethr_memory_allocators", "ethr_late_init_data", "ethr_mutex_opt",
    "ethr_cond_opt", "ethr_rwmutex_opt",
)
PORTABLE_ENUMS = ("ethr_rwmutex_type", "ethr_rwmutex_lived")
FLAGS = ("ETHR_THR_NAME_MAX", *(f"ETHR_TS_EV_{name}" for name in (
    "ETHREAD", "INITED", "TMP", "MAIN_THR", "BUSY", "PEEK")),
    "ETHR_LoadLoad", "ETHR_LoadStore", "ETHR_StoreLoad", "ETHR_StoreStore")
SCALARS = ("ethr_sint32_t", "ethr_uint32_t", "ethr_sint64_t", "ethr_uint64_t",
           "ethr_sint_t", "ethr_uint_t")
DW_ORDER = ("ETHR_DW_SINT_LOW_WORD", "ETHR_DW_SINT_HIGH_WORD")
NATIVE_SCALAR_OPS = ("cmpxchg", "xchg", "set", "init", "add_return", "read",
                     "inc_return", "dec_return", "add", "inc", "dec", "and_retold", "or_retold")
NATIVE_PRIMITIVES = tuple(
    [f"ethr_native_atomic{width}_{operation}_mb" for width in (32, 64) for operation in NATIVE_SCALAR_OPS]
    + [f"ethr_native_atomic{width}_addr" for width in (32, 64)]
    + [f"ethr_native_dw_atomic_{operation}_mb" for operation in ("init", "set", "read", "cmpxchg")]
    + ["ethr_native_dw_atomic_addr"])


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def uncomment(source):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)


def unique(matches, label):
    if len(matches) != 1:
        raise ValueError(f"expected one pinned {label}, found {len(matches)}")
    return matches[0]


def declarations(source):
    """Read plain public prototypes, without enabling any platform branches.

    This intentionally isn't a general C parser: pinned declarations use these
    return types. Balanced parentheses retain nested thread-entry signatures;
    bodies and preprocessor lines never become declarations. Newly unsupported
    syntax fails the subsequent complete local-function coverage check.
    """
    pattern = r"^((?:int|void|char|size_t|ethr_\w+|ETHR_PROTO_NORETURN__)\s*\**\s*)(ethr_\w+)\s*\("
    found = {}
    for match in re.finditer(pattern, source, re.M):
        end, depth = match.end(), 1
        while end < len(source) and depth:
            depth += (source[end] == "(") - (source[end] == ")")
            end += 1
        tail = end
        while tail < len(source) and source[tail].isspace():
            tail += 1
        if depth or tail == len(source) or source[tail] != ";":
            continue
        name = match[2]
        text = source[match.start():tail + 1]
        if name in found and found[name] != text:
            raise ValueError(f"ambiguous pinned declaration: {name}")
        found[name] = text
    return found


def layout_checks(source):
    lines, records = [], {}
    for name in PORTABLE_STRUCTS:
        body = unique(re.findall(r"typedef\s+struct\s*\{([^{}]*)\}\s*" + name + r"\s*;",
                                 source, re.S), name)
        records[name] = body
    records["ethr_ts_event"] = unique(re.findall(
        r"struct\s+ethr_ts_event_\s*\{([^{}]*)\}\s*;", source, re.S), "ethr_ts_event")
    for name, body in records.items():
        # Members keep the adapter's opaque platform types. These assertions
        # check the enclosing portable record, not pthread/native layouts.
        lines.append(f"typedef struct {{{body}}} otp_{name};")
        lines.append(f'_Static_assert(sizeof({name}) == sizeof(otp_{name}), "{name} size");')
        lines.append(f'_Static_assert(_Alignof({name}) == _Alignof(otp_{name}), "{name} alignment");')
        fields = []
        for member in body.split(";"):
            if not member.strip():
                continue
            callback = re.search(r"\(\s*\*\s*(\w+)\s*\)", member)
            field = callback[1] if callback else re.findall(r"\w+", member)[-1]
            fields.append(field)
            lines.append(f'_Static_assert(offsetof({name}, {field}) == offsetof(otp_{name}, {field}), "{name}.{field} offset");')
            lines.append(f'_Static_assert(__builtin_types_compatible_p(__typeof__((({name} *)0)->{field}), __typeof__(((otp_{name} *)0)->{field})), "{name}.{field} type");')
        records[name] = fields
    for name in PORTABLE_ENUMS:
        body = unique(re.findall(r"typedef\s+enum\s*\{([^{}]*)\}\s*" + name + r"\s*;",
                                 source, re.S), name)
        constants = re.findall(r"\bETHR_\w+\b", body)
        renamed = re.sub(r"\bETHR_(\w+)\b", r"OTP_ETHR_\1", body)
        lines.append(f"typedef enum {{{renamed}}} otp_{name};")
        lines.append(f'_Static_assert(sizeof({name}) == sizeof(otp_{name}), "{name} size");')
        lines.append(f'_Static_assert(_Alignof({name}) == _Alignof(otp_{name}), "{name} alignment");')
        for constant in constants:
            lines.append(f'_Static_assert((int){constant} == (int)OTP_{constant}, "{constant}");')
    for name in FLAGS:
        value = unique(re.findall(r"^#define\s+" + name + r"\s+([^\n]+)", source, re.M), name)
        lines.append(f'_Static_assert({name} == ({value}), "{name}");')
    return "\n".join(lines), records


def scalar_checks(header):
    start = header.index("#ifndef ETHR_HAVE_INT32_T")
    end = header.index("#if defined(ETHR_SIZEOF___INT128_T)", start)
    definitions = re.sub(r"\bethr_(\w+)\b", r"otp_ethr_\1", header[start:end])
    definitions = re.sub(r"\bETHR_HAVE_INT(32|64)_T\b", r"OTP_ETHR_HAVE_INT\1_T", definitions)
    # Evaluate OTP's real typedef branches with compiler-derived target sizes.
    # This supplies no configure feature answers and selects no OS backend.
    sizes = {"INT": "INT", "LONG": "LONG", "LONG_LONG": "LONG_LONG", "PTR": "POINTER"}
    lines = [f"#define ETHR_SIZEOF_{name} __SIZEOF_{builtin}__" for name, builtin in sizes.items()]
    lines.extend((definitions, *(f'_Static_assert(__builtin_types_compatible_p({name}, otp_{name}), "{name}");'
                                for name in SCALARS)))
    return "\n".join(lines)


def dw_value_checks(header):
    body = unique(re.findall(r"typedef\s+union\s*\{([^{}]*)\}\s*ethr_dw_sint_t\s*;",
                             header, re.S), "double-word value union")
    order = unique(re.findall(r"(#ifdef ETHR_BIGENDIAN\n(?=#\s*define ETHR_DW_SINT_LOW_WORD\b).*?#endif)",
                              header, re.S), "double-word word order")
    order = re.sub(r"\bETHR_DW_SINT_(LOW|HIGH)_WORD\b", r"OTP_ETHR_DW_SINT_\1_WORD", order)
    return (f"typedef union {{{body}}} otp_ethr_dw_sint_t;\n" + order + "\n" +
            '_Static_assert(sizeof(ethr_dw_sint_t) == sizeof(otp_ethr_dw_sint_t), "double-word value size");\n'
            '_Static_assert(_Alignof(ethr_dw_sint_t) == _Alignof(otp_ethr_dw_sint_t), "double-word value alignment");\n'
            '_Static_assert(offsetof(ethr_dw_sint_t, sint) == offsetof(otp_ethr_dw_sint_t, sint), "double-word array offset");\n'
            '_Static_assert(__builtin_types_compatible_p(__typeof__(((ethr_dw_sint_t *)0)->sint), __typeof__(((otp_ethr_dw_sint_t *)0)->sint)), "double-word array type");\n' +
            "\n".join(f'_Static_assert({name} == OTP_{name}, "{name}");' for name in DW_ORDER))


def check_record_members(ast, expected):
    records = {}
    nodes = {node["id"]: node for node in ast["inner"] if node["kind"] == "RecordDecl"}
    for node in nodes.values():
        if not node.get("completeDefinition"):
            continue
        fields = [field["name"] for field in node.get("inner", []) if field["kind"] == "FieldDecl"]
        while node:
            records[node["id"]] = fields
            node = nodes.get(node.get("previousDecl"))

    def tag_id(node):
        tag = node.get("ownedTagDecl", node.get("decl", {}))
        if tag.get("kind") == "RecordDecl":
            return tag["id"]
        for child in node.get("inner", []):
            if result := tag_id(child):
                return result
        return None

    for node in ast["inner"]:
        name = node.get("name")
        if node["kind"] == "TypedefDecl" and name in expected:
            actual = records.get(tag_id(node))
            if actual != expected[name]:
                raise SystemExit(f"{name} member order differs: {actual} != {expected[name]}")


def run_logged(command, log, *, rejection=None):
    with log.open("w") as stream:
        result = subprocess.run([str(arg) for arg in command], cwd=ROOT,
                                stdout=stream, stderr=subprocess.STDOUT)
    if rejection is None:
        if result.returncode:
            raise SystemExit(f"command failed ({result.returncode}); see {log}")
    elif not result.returncode or rejection not in log.read_text():
        raise SystemExit(f"negative control did not reject for {rejection!r}; see {log}")
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--otp-source", type=Path, required=True)
    parser.add_argument("--picolibc-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--upstream-header", action="store_true",
                        help="also compile through the staged Knytos selection in OTP's full ethread.h")
    args = parser.parse_args()
    source, out = args.otp_source.resolve(), args.output.resolve()
    revision = subprocess.check_output(["git", "-C", source, "rev-parse", "HEAD"], text=True).strip()
    if revision != OTP_PIN:
        parser.error("OTP must be at the pinned 29.1.2 revision")
    out.mkdir(parents=True, exist_ok=False)
    upstream, upstream_hashes = [], {}
    for name in UPSTREAM_HEADERS:
        content = subprocess.check_output(["git", "-C", source, "show", f"{OTP_PIN}:{name}"])
        upstream_hashes[name] = hashlib.sha256(content).hexdigest()
        upstream.append(uncomment(content.decode()))
    joined = "\n".join(upstream)
    prototypes = {}
    for header in upstream:
        for name, declaration in declarations(header).items():
            if name in prototypes:
                raise ValueError(f"duplicate upstream header declaration: {name}")
            prototypes[name] = declaration
    archive = out / "libknytos_ethread.a"
    run_logged(["python3", ROOT / "ports/erts/build.py", "--picolibc-source",
                args.picolibc_source.resolve(), "--output", archive], out / "archive.txt")
    manifest = json.loads(archive.with_suffix(".build.json").read_text())
    flags = ["clang", *manifest["compiler_flags"], "-I", ROOT / "ports/erts"]
    local = out / "local.c"
    local.write_text('#include "knytos_ethread.h"\n')
    with (out / "local-ast.json").open("w") as stream:
        subprocess.run([*map(str, flags), "-fsyntax-only", "-Xclang", "-ast-dump=json", str(local)],
                       cwd=ROOT, stdout=stream, check=True)
    ast = json.loads((out / "local-ast.json").read_text())
    functions = sorted({node["name"] for node in ast["inner"]
                        if node["kind"] == "FunctionDecl" and node.get("name", "").startswith("ethr_")
                        and node["name"] not in NATIVE_PRIMITIVES})
    missing = set(functions) - prototypes.keys()
    if missing:
        raise SystemExit(f"local functions without pinned public prototypes: {sorted(missing)}")
    layouts, records = layout_checks(joined)
    check_record_members(ast, records)
    selection = None
    selected_flags = flags
    include_header = "knytos_ethread.h"
    if args.upstream_header:
        include, profile, selection = stage_headers(source, out)
        selected_flags = ["clang", "-I", include, "-include", profile, *flags[1:]]
        include_header = "ethread.h"
    # Retained address references force link resolution of every covered API,
    # including local inline atomics. No function or initializer is executed.
    references = "\n".join(f'__attribute__((used, retain)) static __typeof__(&{name}) const ref_{name} = &{name};'
                           for name in (*functions, *NATIVE_PRIMITIVES))
    contract = (f"/* SPDX-License-Identifier: Apache-2.0\n"
                f" * OTP declaration slices: Ericsson AB, OTP revision {OTP_PIN}.\n"
                f" * Generated by ports/erts/check-interface.py; not a platform header. */\n"
                f'#include "{include_header}"\n#include <stddef.h>\n' + scalar_checks(upstream[0]) + "\n" +
                "\n".join(prototypes[name] for name in functions) + "\n" + layouts + "\n" +
                dw_value_checks(upstream[1]) + "\n" + references +
                "\nint main(void) {\n"
                "    ETHR_COMPILER_BARRIER;\n    ETHR_MEMORY_BARRIER;\n"
                "    ETHR_WRITE_MEMORY_BARRIER;\n    ETHR_READ_MEMORY_BARRIER;\n"
                "    ETHR_READ_DEPEND_MEMORY_BARRIER;\n"
                "    for (unsigned mask = 0; mask < 16; ++mask) ETHR_MEMBAR(mask);\n"
                "    return 0;\n}\n")
    contract = "\n".join(line.rstrip() for line in contract.splitlines()) + "\n"
    consumer = out / "contract.c"
    consumer.write_text(contract)
    run_logged([*selected_flags, "-fsyntax-only", consumer], out / "interface.txt")
    if selection:
        dependencies = out / "header-dependencies.d"
        run_logged([*selected_flags, "-M", "-MF", dependencies, consumer], out / "dependencies.txt")
        deptext = dependencies.read_text()
        expected = str(include / "ethread.h").replace(" ", "\\ ")
        if expected not in deptext or "/pthread/" in deptext or "/win/" in deptext:
            raise SystemExit("consumer did not select the staged Knytos header path")
        selection["dependencies_sha256"] = digest(dependencies)
        for label, options in (("inline", ["-DETHR_TRY_INLINE_FUNCS=1"]),
                               ("debug", ["-DETHR_TRY_INLINE_FUNCS=1", "-DDEBUG"])):
            run_logged([*selected_flags, *options, "-fsyntax-only", consumer], out / f"{label}.txt")
    controls = {}
    mutations = {
        "signature": ("int ethr_cond_wait(ethr_cond *, ethr_mutex *);",
                      "void ethr_cond_wait(ethr_cond *, ethr_mutex *);", "conflicting types for 'ethr_cond_wait'"),
        "layout": ("short mtix;", "int mtix;", "ethr_ts_event.mtix type"),
    }
    for label, (before, after, diagnostic) in mutations.items():
        if contract.count(before) != 1:
            raise ValueError(f"unexpected pinned {label} control anchor")
        path = out / f"reject-{label}.c"
        path.write_text(contract.replace(before, after))
        controls[label] = run_logged([*selected_flags, "-fsyntax-only", path],
                                    out / f"reject-{label}.txt", rejection=diagnostic)
    if selection:
        for label, option, diagnostic in (
                ("backend", "-DETHR_PTHREADS=1", "Knytos ethread cannot select another thread backend"),
                ("profile", "-UKNYTOS_WORKER_LIBC", "Knytos ethread requires the worker-libc profile")):
            controls[label] = run_logged([*selected_flags, option, "-fsyntax-only", consumer],
                                        out / f"reject-{label}.txt", rejection=diagnostic)
    elf = out / "contract.elf"
    include_flags = ["--include", ROOT / "ports/erts"]
    if selection:
        include_flags = ["--include", include, *include_flags]
        include_flags.extend(("--define", "ETHR_HAVE_ETHREAD_DEFINES=1",
                              "--define", "ETHR_KNYTOS=1", "--define", "ETHR_SIZEOF_INT=__SIZEOF_INT__",
                              "--define", "ETHR_SIZEOF_LONG=__SIZEOF_LONG__",
                              "--define", "ETHR_SIZEOF_LONG_LONG=__SIZEOF_LONG_LONG__",
                              "--define", "ETHR_SIZEOF_PTR=__SIZEOF_POINTER__",
                              "--define", "ASSUMED_CACHE_LINE_SIZE=64"))
    run_logged(["python3", ROOT / "ports/libc/build.py", consumer, "--worker-libc",
                "--picolibc-source", args.picolibc_source.resolve(), *include_flags,
                "--library", archive, "--output", elf], out / "link.txt")
    link_map = elf.with_suffix(".build") / "link-map.txt"
    linked = link_map.read_text()
    for name in (*functions, *NATIVE_PRIMITIVES):
        if f"ref_{name}\n" not in linked:
            raise SystemExit(f"retained API reference missing from link map: {name}")
    for member in manifest["members_sha256"]:
        if f"({member})" not in linked:
            raise SystemExit(f"archive member missing from link map: {member}")
    evidence = dict(schema="knytos.erts-interface.v1", otp_revision=OTP_PIN,
                    upstream_sha256=upstream_hashes, functions=functions,
                    portable_records=records, portable_enums=list(PORTABLE_ENUMS),
                    scalars=list(SCALARS), constants=list(FLAGS + DW_ORDER), rejection_status=controls,
                    portable_value_unions={"ethr_dw_sint_t": ["sint"]},
                    native_callable_names=list(NATIVE_PRIMITIVES),
                    retained_reference_count=len(functions) + len(NATIVE_PRIMITIVES),
                    archive=manifest, elf_sha256=digest(elf),
                    contract_sha256=digest(consumer), link_map_sha256=digest(link_map),
                    checker_inputs_sha256={str(path.relative_to(ROOT)): digest(path) for path in (
                        Path(__file__).resolve(), ROOT / "ports/erts/configure.py", ROOT / "ports/erts/stage_headers.py")},
                    header_selection=selection)
    (out / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Matched {len(functions)} public functions and {len(records)} portable records; "
          f"linked {len(NATIVE_PRIMITIVES)} native callable names; {len(controls)} controls rejected")


if __name__ == "__main__":
    main()
