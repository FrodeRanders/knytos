#!/usr/bin/env python3
"""Check CI kernel artifacts and pinned Sitas wait-ABI inputs."""

from pathlib import Path
import re
import subprocess
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[2]
SITAS = ROOT / ".dependencies/sitas"


def checked_wait_number(source):
    match = re.search(r"const SYSCALL_CQ_WAIT_CHECKED:\s*u16\s*=\s*(\d+);", source)
    if match is None:
        raise ValueError("Sitas lacks the mandatory checked CQ wait")
    retired = re.findall(r"const SYSCALL_CQ_WAIT\w*:\s*u16\s*=\s*(12|42);", source)
    if retired:
        raise ValueError("Sitas still declares retired CQ waits")
    return int(match.group(1))


class CiRuntimeInputs(unittest.TestCase):
    def setUp(self):
        self.workflow = (ROOT / ".github/workflows/ci.yml").read_text()

    def test_pinned_dependency_uses_the_current_kernel_wait_number(self):
        subprocess.run(['python3', str(ROOT / 'scripts/resolve-dependencies.py'), '--check'], check=True)
        syscall = (ROOT / "crates/knytos-syscall/src/lib.rs").read_text()
        number = re.search(r"\(CqWaitChecked,\s*(\d+)\)", syscall)
        self.assertIsNotNone(number)
        self.assertEqual(checked_wait_number((SITAS / 'crates/sitas-knytos/src/lib.rs').read_text()), int(number.group(1)))

    def test_previous_ci_pin_is_rejected(self):
        # Regression control: the previous backend declared only the retired waits.
        source = 'const SYSCALL_CQ_WAIT: u16 = 12;\nconst SYSCALL_CQ_WAIT_TIMED: u16 = 42;'
        with self.assertRaisesRegex(ValueError, "lacks the mandatory checked CQ wait"):
            checked_wait_number(source)

    def test_every_dependency_checkout_uses_the_shared_pin(self):
        self.assertNotIn('SITAS_REV:', self.workflow)
        self.assertNotIn('gautelis/sitas', self.workflow)
        # Every job using Cargo's workspace needs preparation in its own checkout.
        jobs = re.split(r'(?m)^  [a-z-]+:\n', self.workflow)[1:]
        count = 0
        for job in jobs:
            if any(marker in job for marker in ('cargo fmt ', 'cargo clippy ', 'cargo build ',
                                                'scripts/run-host-tests.sh', 'scripts/build-knytos-services.sh',
                                                'Boot QEMU and verify')):
                self.assertIn('python3 scripts/resolve-dependencies.py --fetch', job)
                count += 1
        self.assertGreaterEqual(count, 6)

    def test_ci_warning_flags_preserve_kernel_frame_pointers(self):
        self.assertNotRegex(self.workflow, r"(?m)^\s*RUSTFLAGS:")
        self.assertIn('CARGO_BUILD_RUSTFLAGS: "-D warnings"', self.workflow)
        config = tomllib.loads((ROOT / ".cargo/config.toml").read_text())
        for arch in ("aarch64", "x86_64"):
            flags = config["target"][f"{arch}-unknown-none-knytos"]["rustflags"]
            self.assertIn("force-frame-pointers=yes", flags)
            self.assertIn("warnings", flags)
            self.assertIn("-D", flags)

    def test_ci_toolchains_match_the_repository_pin(self):
        channel = tomllib.loads((ROOT / "rust-toolchain.toml").read_text())["toolchain"]["channel"]
        count = 0
        for workflow in (ROOT / ".github/workflows").glob("*.yml"):
            for pin in re.findall(r"^\s+toolchain:\s+(\S+)\s*$", workflow.read_text(), re.MULTILINE):
                with self.subTest(workflow=workflow.name, toolchain=pin):
                    self.assertEqual(pin, channel)
                count += 1
        self.assertGreater(count, 0)

    def test_ci_inspects_the_declared_kernel_binary_instead_of_the_package_name(self):
        kernel = tomllib.loads((ROOT / "crates/catten/Cargo.toml").read_text())
        binaries = {target["name"] for target in kernel["bin"]}
        paths = [quoted or bare for quoted, bare in
                 re.findall(r'--kernel\s+(?:"([^"\n]+)"|(\S+))', self.workflow)]
        paths.extend(re.findall(r'check-kernel-asm-sections\.py\s*\\\s*\n\s*(\S+)', self.workflow))
        self.assertGreaterEqual(len(paths), 3, "matrix, HVF and assembly checks must be covered")
        for path in paths:
            with self.subTest(path=path):
                self.assertIn(Path(path).name, binaries,
                              "CI must inspect Cargo's binary output, not its package name")


if __name__ == "__main__":
    unittest.main()
