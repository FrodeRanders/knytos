#!/usr/bin/env python3
"""Check the barrier fixture's actual AArch64 ELF instruction sites."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SITES = ("full", "write", "read", "depend", *(f"mask_{n}" for n in range(16)))


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
        label = re.fullmatch(r"[0-9a-f]+ <(knytos_barrier_\w+)>:", line)
        if re.match(r"[0-9a-f]+ <", line):
            current = label[1] if label else None
            if current:
                bodies[current] = []
                excerpts.append(line)
        elif current and (instruction := re.match(r"\s*[0-9a-f]+:\s*(.+)", line)):
            bodies[current].append(" ".join(instruction[1].split()))
            excerpts.append(line)
    for site in SITES:
        name = f"knytos_barrier_{site}"
        if bodies.get(name) != ["dmb ish", "ret"]:
            raise SystemExit(f"{name}: expected a full dmb ish site, got {bodies.get(name)}")
    compiler = bodies.get("knytos_barrier_compiler", [])
    if any(re.match(r"(?:dmb|dsb|isb)\b", instruction) for instruction in compiler):
        raise SystemExit("compiler-only barrier unexpectedly emitted a CPU barrier")
    patterns = (r"str w\d+, \[x0\]", r"ldr w\d+, \[x0\]", r"str w\d+, \[x1\]")
    memory = [instruction for instruction in compiler if re.match(r"(?:str|ldr)\b", instruction)]
    if len(memory) != 3 or not all(re.fullmatch(pattern, instruction) for pattern, instruction in zip(patterns, memory)):
        raise SystemExit(f"compiler barrier did not preserve store/reload/store: {compiler}")
    result = dict(schema="knytos.erts-barrier-codegen.v1",
                  elf_sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(),
                  checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  objdump_version=subprocess.check_output([str(objdump), "--version"], text=True).strip(),
                  instructions=bodies)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    args.output.with_suffix(".txt").write_text("\n".join(excerpts) + "\n")
    print("Verified 20 full data-fence sites and compiler-only store/reload/store in AArch64 ELF")


if __name__ == "__main__":
    main()
