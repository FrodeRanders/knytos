#!/usr/bin/env python3
"""Execute C fixtures through ordinary signed, TLS-authorized deployments."""
import argparse
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
CASES = [("c-hello", 0), ("c-memory", 1), ("c-format", 2), ("c-clock", 3),
         ("c-startup", 4), ("c-exit", 5), ("c-error", 6), ("c-abort", 7), ("c-empty", 8), ("c-survivor", 0)]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--picolibc-source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--firmware", type=Path)
    args = parser.parse_args()
    def interrupted(_signal, _frame):
        raise SystemExit("C runtime campaign interrupted")
    signal.signal(signal.SIGTERM, interrupted)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    record = {"schema": "knytos.c-runtime-m1.v1", "cases": [], "negative_controls": [], "outcome": "running"}
    def save():
        (output / "result.json").write_text(json.dumps(record, indent=2) + "\n")
    def command(arguments, name, env=None, timeout=900, expected_failure=False):
        with (output / (name + ".txt")).open("w") as log:
            completed = subprocess.run([str(a) for a in arguments], cwd=ROOT, env=env,
                                       stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        if bool(completed.returncode) != expected_failure:
            raise RuntimeError(f"{name}: unexpected exit {completed.returncode}")
        return completed.returncode
    save()
    try:
        images = output / "bundle"
        images.mkdir()
        for name, number in CASES:
            command(["python3", ROOT / "ports/libc/build.py", ROOT / ("ports/libc/tests/empty.c" if number == 8 else "ports/libc/tests/suite.c"),
                     "--define", f"CASE={number}", "--picolibc-source", args.picolibc_source,
                     "--output", images / (name + ".elf")], name + "-build")
        for source, diagnostic in (("tls-rejected.c", "TLS is unsupported"),
                                   ("unsupported-rejected.c", "undefined symbol: open")):
            name = source.removesuffix(".c")
            command(["python3", ROOT / "ports/libc/build.py", ROOT / "ports/libc/tests" / source,
                     "--picolibc-source", args.picolibc_source, "--output", output / (name + ".elf")],
                    name, expected_failure=True)
            assert diagnostic in (output / (name + ".txt")).read_text(), name
            record["negative_controls"].append({"name": name, "expected_diagnostic": diagnostic, "passed": True})
        command([ROOT / "scripts/run-aarch64.sh", "release", "--minimal", "--build-only", "--instance", "cabi-m1"], "baseline-build")
        signer = ROOT / "target/debug/cluster-sign"
        key = ROOT / "tools/cluster-sign/dev-key.hex"
        for name, _ in CASES:
            image = images / (name + ".elf")
            command([signer, "elf-sign", image, name, key, "service", "1", "1", "0x7", "-"], name + "-sign")
            command([signer, "deployment-sign", output / (name + ".cdep"), name, "fixture/" + name,
                     image, "0", "1", "16", "1", "5000", key], name + "-descriptor")
        tampered = bytearray((images / "c-hello.elf").read_bytes())
        command([signer, "elf-verify", images / "c-hello.elf", "c-hello",
                 "3ddc95c26bd5f4022d95a4c6c8d074f577f11af7873e527b018b21be2c035463"], "signature-accepted")
        # Change a covered executable byte while keeping a well-formed ELF.
        import struct
        phoff = struct.unpack_from("<Q", tampered, 32)[0]
        text_offset = struct.unpack_from("<Q", tampered, phoff + 8)[0]
        tampered[text_offset] ^= 1
        (output / "tampered.elf").write_bytes(tampered)
        command([signer, "elf-verify", output / "tampered.elf", "c-hello",
                 "3ddc95c26bd5f4022d95a4c6c8d074f577f11af7873e527b018b21be2c035463"],
                "signature-rejected", expected_failure=True)
        record["negative_controls"].append({"name": "signature-rejected", "passed": True})
        for image in (ROOT / "target/embedded-services/aarch64-unknown-none-minimal").glob("*.elf"):
            shutil.copyfile(image, images / image.name)
        command(["python3", ROOT / "scripts/make-nvme-image.py", output / "disk.img", images], "disk")
        boot = ROOT / "os-images/knytos-aarch64-release-cabi-m1-minimal.img"
        shutil.copyfile(boot, output / "boot.img")
        command(["mcopy", "-i", output / "boot.img", "::/knytos", output / "kernel.elf"], "extract-kernel")
        # Immutable debugger symbols describe the selected payload, not a later build.
        llvm = Path(subprocess.check_output(["rustc", "--print", "sysroot"], cwd=ROOT, text=True).strip())
        host = subprocess.check_output(["rustc", "-vV"], cwd=ROOT, text=True).split("host: ")[1].splitlines()[0]
        symbols = subprocess.check_output([llvm / "lib/rustlib" / host / "bin/llvm-nm", "--print-size", "--demangle", output / "kernel.elf"], text=True)
        (output / "kernel.symbols").write_text(symbols)
        qemu = shutil.which("qemu-system-aarch64")
        firmware = args.firmware or Path(qemu).resolve().parents[1] / "share/qemu/edk2-aarch64-code.fd"
        if not firmware.is_file():
            firmware = Path("/opt/homebrew/share/qemu/edk2-aarch64-code.fd")
        if not firmware.is_file():
            raise RuntimeError("pass --firmware with edk2-aarch64-code.fd")
        credentials = Path(str(boot) + ".management")
        environment = dict(os.environ, KNYTOS_MANAGEMENT_CA_FILE=str(credentials / "ca.pem"),
                           KNYTOS_MANAGEMENT_CERT_FILE=str(credentials / "administrator.pem"),
                           KNYTOS_MANAGEMENT_KEY_FILE=str(credentials / "administrator.key"),
                           KNYTOS_MANAGEMENT_SERVER_NAME="127.0.0.1")
        record.update(kernel_sha256=digest(output / "kernel.elf"), firmware_sha256=digest(firmware),
                      qemu_version=subprocess.check_output([qemu, "--version"], text=True).strip(),
                      initial_disk_sha256=digest(output / "disk.img"), boot_image_sha256=digest(output / "boot.img"),
                      images_sha256={p.name: digest(p) for p in sorted(images.glob("*.elf"))})
        spec = importlib.util.spec_from_file_location("boot_diagnostics", ROOT / "scripts/boot-diagnostics.py")
        diagnostic = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(diagnostic)
        with tempfile.TemporaryDirectory(prefix="cabi-qmp-", dir="/tmp") as transport:
            serial = output / "serial.txt"
            options = ["-M", "virt,gic-version=3,msi=gicv2m,iommu=smmuv3,default-bus-bypass-iommu=off",
                       "-m", "512M", "-bios", str(firmware), "-cpu", "cortex-a710", "-smp", "4",
                       "-drive", f"file={output / 'boot.img'},format=raw,if=none,id=boot0",
                       "-device", "virtio-blk-pci,drive=boot0,addr=3,bootindex=1",
                       "-object", "rng-random,filename=/dev/urandom,id=knytos-rng",
                       "-device", "virtio-rng-pci,rng=knytos-rng,disable-legacy=on,iommu_platform=on,addr=4",
                       "-drive", f"if=none,file={output / 'disk.img'},format=raw,id=nvme0",
                       "-device", "nvme,drive=nvme0,serial=cat0,max_ioqpairs=4,addr=2", "-nic", "none",
                       "-netdev", "user,id=knytos-net,hostfwd=tcp::28080-:443,hostfwd=tcp::28081-:7444",
                       "-device", "virtio-net-pci,netdev=knytos-net,disable-legacy=on,iommu_platform=on,mac=52:54:00:12:34:56,addr=1,romfile=",
                       "-display", "none", "-serial", f"file:{serial}",
                       "-qmp", f"unix:{transport}/qmp.sock,server=on,wait=off",
                       "-gdb", f"unix:{transport}/gdb.sock,server=on,wait=off"]
            record["qemu_arguments"] = options
            save()
            with (output / "qemu.txt").open("w") as log:
                process = subprocess.Popen([qemu, *options], stdout=log, stderr=subprocess.STDOUT)
                def wait_for(predicate, seconds):
                    deadline = time.monotonic() + seconds
                    while time.monotonic() < deadline:
                        if process.poll() is not None:
                            raise RuntimeError("guest exited early")
                        text = serial.read_text(errors="replace") if serial.exists() else ""
                        if "Kernel panic:" in text:
                            raise RuntimeError("kernel panic")
                        if predicate(text):
                            return text
                        time.sleep(0.1)
                    raise TimeoutError("native C observation deadline")
                try:
                    wait_for(lambda s: "BOOT COMPLETE:" in s and "[deployd] listening" in s
                             and "[time] synchronized" in s and "[dns] proposed filtered capacity" in s, 180)
                    for name, number in CASES:
                        before = len(serial.read_text(errors="replace"))
                        command([signer, "deployment-notify", output / (name + ".cdep"), "127.0.0.1:28081"], name + "-admission", env=environment, timeout=40)
                        expected = "CABI expected abort" if number == 7 else "CABI expected nonzero" if number == 6 else "CABI explicit exit" if number == 5 else f"CABI PASS case={number}"
                        def complete(text):
                            tail = text[before:]
                            launch = re.search(r'\[agent\] launched "' + re.escape(name) + r'" generation=\d+ asid=(\d+)', tail)
                            if not launch:
                                return False
                            domain_lines = "\n".join(line for line in tail.splitlines() if re.search(r'asid=' + launch[1] + r'\b', line))
                            terminal = "[c-runtime] abort" if number == 7 else f"[c-runtime] exit={17 if number == 6 else 0} "
                            return expected in domain_lines and terminal in domain_lines
                        text = wait_for(complete, 60)[before:]
                        if number not in (7, 8):
                            assert "CABI FINI" in text, name
                        if number != 7:
                            assert "remaining_blocks=0" in text, name
                        if number in (4, 5, 6):
                            assert "released_blocks=1" in text, name
                        if number in (4, 5):
                            assert text.index("CABI ATEXIT") < text.index("CABI FINI"), name
                        record["cases"].append({"name": name, "case": number, "passed": True, "observation": text})
                        save()
                    record["outcome"] = "passed"
                except BaseException:
                    record["snapshot"] = diagnostic.capture(output, {"serial_log": str(serial), "transport": transport, "cpus": 4}, "C-runtime-fixture-failure")
                    raise
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
    except BaseException as error:
        record.update(outcome="failed", error=repr(error))
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
