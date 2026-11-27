#!/usr/bin/env python3
"""Check native double-word operations in the linked AArch64 fixture."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    host = subprocess.check_output(["rustc", "-vV"], cwd=ROOT, text=True).split("host: ")[1].splitlines()[0]
    sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], cwd=ROOT, text=True).strip())
    objdump = sysroot / "lib/rustlib" / host / "bin/llvm-objdump"
    listing = subprocess.check_output([str(objdump), "--disassemble", "--no-show-raw-insn", str(args.elf)], text=True)
    bodies, excerpts = {}, []
    current = None
    for line in listing.splitlines():
        label = re.fullmatch(r"[0-9a-f]+ <(knytos_dw_(?:set|read|cas))>:", line)
        if re.match(r"[0-9a-f]+ <", line):
            current = label[1] if label else None
            if current:
                bodies[current] = []
                excerpts.append(line)
        elif current and (instruction := re.match(r"\s*[0-9a-f]+:\s*(.+)", line)):
            bodies[current].append(" ".join(instruction[1].split()))
            excerpts.append(line)
    for name in ("knytos_dw_set", "knytos_dw_read", "knytos_dw_cas"):
        instructions = bodies.get(name, [])
        fences = [i for i, text in enumerate(instructions) if text == "dmb ish"]
        loads = [i for i, text in enumerate(instructions) if text.startswith("ldaxp ")]
        stores = [i for i, text in enumerate(instructions) if text.startswith("stlxp ")]
        if (len(fences) != 2 or not loads or not stores
                or not fences[0] < min(loads + stores) <= max(loads + stores) < fences[1]
                or not any(text.startswith("cbnz ") for text in instructions)):
            raise SystemExit(f"{name}: missing fenced native exclusive-pair loop: {instructions}")
        for text in instructions:
            if text.startswith("blr ") or (text.startswith("bl ") and not re.search(r"<memcpy(?:\+0x[0-9a-f]+)?>", text)):
                raise SystemExit(f"{name}: unexpected non-native operation helper: {text}")
    read = bodies["knytos_dw_read"]
    pair = re.fullmatch(r"ldaxp (x\d+), (x\d+), (\[x\d+\])", next(text for text in read if text.startswith("ldaxp ")))
    if not pair or not any(re.fullmatch(r"stlxp w\d+, " + re.escape(", ".join(pair.groups())), text) for text in read):
        raise SystemExit("double-word read did not validate its loaded pair with an exclusive writeback")
    if sum(text.startswith("stlxp ") for text in bodies["knytos_dw_cas"]) < 2:
        raise SystemExit("CAS lacks a validation path for a mismatched pair")
    result = dict(schema="knytos.erts-dw-codegen.v1",
                  elf_sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(),
                  checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  objdump_version=subprocess.check_output([str(objdump), "--version"], text=True).strip(),
                  instructions=bodies)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    args.output.with_suffix(".txt").write_text("\n".join(excerpts) + "\n")
    print("Verified native set/read/CAS exclusive-pair loops, read validation and full surrounding fences")


if __name__ == "__main__":
    main()
