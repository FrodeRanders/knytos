#!/usr/bin/env python3
"""Reject an EFI loader or kernel for a different native QEMU architecture."""

import argparse
from pathlib import Path
import struct
import sys

MACHINES = {"aarch64": (0xAA64, 183), "x86_64": (0x8664, 62)}


def check(arch, efi, kernel):
    pe_machine, elf_machine = MACHINES[arch]
    with Path(efi).open("rb") as stream:
        dos = stream.read(64)
        if len(dos) != 64 or dos[:2] != b"MZ":
            raise ValueError("EFI loader lacks a DOS/PE header")
        stream.seek(struct.unpack_from("<I", dos, 60)[0])
        pe = stream.read(94)
    if len(pe) != 94 or pe[:4] != b"PE\0\0":
        raise ValueError("EFI loader lacks a complete PE header")
    actual_pe = struct.unpack_from("<H", pe, 4)[0]
    if actual_pe != pe_machine:
        raise ValueError(f"{arch} EFI machine expected {pe_machine:#06x}, got {actual_pe:#06x}")
    if (struct.unpack_from("<H", pe, 24)[0] != 0x20B
            or struct.unpack_from("<H", pe, 92)[0] != 10):
        raise ValueError("loader is not a PE32+ EFI application")
    with Path(kernel).open("rb") as stream:
        elf = stream.read(64)
    if len(elf) != 64 or elf[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("expected a little-endian ELF64 kernel")
    actual_elf = struct.unpack_from("<H", elf, 18)[0]
    if actual_elf != elf_machine:
        raise ValueError(f"{arch} kernel machine expected {elf_machine}, got {actual_elf}")
    return f">>> Boot payload architecture: {arch} (EFI {actual_pe:#06x}, ELF {actual_elf})"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=MACHINES)
    parser.add_argument("efi", type=Path)
    parser.add_argument("kernel", type=Path)
    args = parser.parse_args()
    try:
        print(check(args.arch, args.efi, args.kernel))
    except (ValueError, OSError, struct.error) as error:
        sys.exit(f"error: boot payload architecture check failed: {error}")


if __name__ == "__main__":
    main()
