#!/usr/bin/env python3
"""Exercise native loader/kernel pairing and reject mixed or malformed inputs."""

import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CHECK = ROOT / "scripts/check-boot-payloads.py"
spec = importlib.util.spec_from_file_location("boot_payloads", CHECK)
payloads = importlib.util.module_from_spec(spec)
spec.loader.exec_module(payloads)
LOADERS = {"aarch64": ROOT / "limine-binary/BOOTAA64.EFI",
           "x86_64": ROOT / "limine-binary/BOOTX64.EFI"}


class BootPayloads(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="knytos-boot-payloads-")
        self.addCleanup(self.temp.cleanup)
        self.kernel = Path(self.temp.name) / "kernel"

    def write_kernel(self, machine):
        elf = bytearray(64)
        elf[:7] = b"\x7fELF\x02\x01\x01"
        struct.pack_into("<H", elf, 18, machine)
        self.kernel.write_bytes(elf)

    def test_shipped_loaders_accept_only_matching_kernel_architecture(self):
        for arch, (_, machine) in payloads.MACHINES.items():
            self.write_kernel(machine)
            with self.subTest(arch=arch):
                self.assertIn(arch, payloads.check(arch, LOADERS[arch], self.kernel))
                other = next(name for name in LOADERS if name != arch)
                with self.assertRaisesRegex(ValueError, "EFI machine expected"):
                    payloads.check(arch, LOADERS[other], self.kernel)
                self.write_kernel(payloads.MACHINES[other][1])
                with self.assertRaisesRegex(ValueError, "kernel machine expected"):
                    payloads.check(arch, LOADERS[arch], self.kernel)

    def test_truncated_invalid_and_wrong_endian_kernels_are_rejected(self):
        for data in (b"", b"\x7fELF\x02\x01\x01", bytes(64),
                     b"\x7fELF\x02\x02\x01" + bytes(57)):
            with self.subTest(data=data):
                self.kernel.write_bytes(data)
                with self.assertRaisesRegex(ValueError, "ELF64"):
                    payloads.check("aarch64", LOADERS["aarch64"], self.kernel)

    def test_malformed_loader_headers_are_rejected(self):
        self.write_kernel(183)
        efi = Path(self.temp.name) / "loader"
        for data in (b"", bytes(64), b"MZ" + bytes(62)):
            with self.subTest(data=data):
                efi.write_bytes(data)
                with self.assertRaisesRegex(ValueError, "header"):
                    payloads.check("aarch64", efi, self.kernel)
        data = bytearray(LOADERS["aarch64"].read_bytes())
        offset = struct.unpack_from("<I", data, 60)[0]
        struct.pack_into("<H", data, offset + 92, 3)  # PE console application.
        efi.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "EFI application"):
            payloads.check("aarch64", efi, self.kernel)

    def test_cli_rejects_mismatched_and_missing_inputs(self):
        self.write_kernel(62)
        result = subprocess.run(["python3", str(CHECK), "aarch64",
                                 str(LOADERS["aarch64"]), str(self.kernel)],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("kernel machine expected 183, got 62", result.stderr)
        self.kernel.unlink()
        result = subprocess.run(["python3", str(CHECK), "aarch64",
                                 str(LOADERS["aarch64"]), str(self.kernel)],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("boot payload architecture check failed", result.stderr)


if __name__ == "__main__":
    unittest.main()
