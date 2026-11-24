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


def domain_logs(text, asid):
    prefix = re.compile(rf"\[EL0 LOG\] lp=\d+ asid={asid} ")
    return "\n".join(line for line in text.splitlines() if prefix.search(line))


def retirement_instance(text, name, generation, mode, schedulers):
    launch = re.search(
        rf'\[agent\] launched "{re.escape(name)}" generation={generation} asid=(\d+)', text)
    if launch is None:
        return None
    lines = domain_logs(text, launch[1])
    if f"{{m3_shutdown,{mode}}}" not in lines:
        return None
    if mode == "busy" and any(
        not re.search(rf"\[EL0 LOG\] lp={slot} asid={launch[1]} \[atomvm\] scheduler entered slot={slot}$",
                      lines, re.MULTILINE) for slot in range(1, schedulers)
    ):
        return None
    return launch[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--picolibc-source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--atomvm-elf", type=Path)
    parser.add_argument("--atomvm-utc-elf", type=Path)
    parser.add_argument("--atomvm-semantics-elf", type=Path)
    parser.add_argument("--atomvm-overflow-elf", type=Path)
    parser.add_argument("--atomvm-scheduler-elf", action="append", type=Path, default=[],
                        help="add an AtomVM scheduler workload using its adjacent build manifest")
    parser.add_argument("--atomvm-idle-scheduler-elf", type=Path,
                        help="add the AtomVM hello fixture with prepared but unused workers")
    parser.add_argument("--atomvm-shutdown-busy-elf", action="append", type=Path, default=[],
                        help="retire an AtomVM VM running continuous Erlang work (one or four schedulers)")
    parser.add_argument("--atomvm-shutdown-parked-elf", type=Path,
                        help="retire an AtomVM VM with unused native workers and no timers")
    parser.add_argument("--native-workers", action="store_true")
    parser.add_argument("--worker-libc", action="store_true")
    parser.add_argument("--smp", type=int, choices=(1, 2, 4), default=4)
    args = parser.parse_args()
    cases = CASES[:-1].copy()
    vm_cases = {
        10: ("avm-hello", args.atomvm_elf, "[atomvm] completed status=0"),
        11: ("avm-utc", args.atomvm_utc_elf, "[atomvm] unsupported: wall clock unavailable"),
        12: ("avm-semantics", args.atomvm_semantics_elf, "[atomvm] completed status=0"),
        13: ("avm-overflow", args.atomvm_overflow_elf, "[atomvm] unsupported: timer deadline overflow"),
    }
    def scheduler_count(image):
        manifest = json.loads(image.with_suffix(".atomvm.json").read_text())
        count = manifest["schedulers"]
        if count not in (1, 2, 4) or count > args.smp or digest(image) != manifest["unsigned_elf_sha256"]:
            parser.error(f"Scheduler image does not match its manifest or guest CPU count: {image}")
        return count
    scheduler_cases = {}
    for image in args.atomvm_scheduler_elf:
        count = scheduler_count(image)
        number = 20 + count
        if number in scheduler_cases:
            parser.error(f"Duplicate scheduler count: {count}")
        scheduler_cases[number] = count
        vm_cases[number] = (f"avm-schedulers-{count}", image, "[atomvm] completed status=0")
    idle_case = 25
    if args.atomvm_idle_scheduler_elf:
        count = scheduler_count(args.atomvm_idle_scheduler_elf)
        if count == 1:
            parser.error("The idle scheduler fixture requires worker threads")
        scheduler_cases[idle_case] = count
        vm_cases[idle_case] = ("avm-idle-schedulers", args.atomvm_idle_scheduler_elf, "[atomvm] completed status=0")
    cases.extend((name, number) for number, (name, path, _) in vm_cases.items() if path)
    worker_cases = {
        14: ("c-workers", 2, "WORKER PASS"),
        15: ("c-unjoined", 2, "WORKER expected unjoined exit"),
        16: ("c-worker-reuse", 9, "WORKER exact join and slot reuse"),
        17: ("c-sync", 9, "SYNC PASS"),
        18: ("c-rwlock", 5, "RWLOCK PASS"),
    }
    libc_cases = {
        19: ("c-worker-libc", 3, "WORKER LIBC PASS"),
        20: ("c-worker-libc-close", 1, "WORKER LIBC expected fixed-stream close rejection"),
    } if args.worker_libc else {}
    worker_cases.update(libc_cases)
    if args.native_workers or args.worker_libc:
        if args.smp < 2:
            parser.error("worker fixtures need at least two guest CPUs")
        cases.extend((name, number) for number, (name, _, _) in worker_cases.items())
    abort_cases = (7, 11, 13, 15, 20)
    cases += CASES[-1:]
    retirement_cases = {}
    retirement_images = [("parked", args.atomvm_shutdown_parked_elf)]
    retirement_images.extend(("busy", path) for path in args.atomvm_shutdown_busy_elf)
    for mode, image in retirement_images:
        if image is None:
            continue
        count = scheduler_count(image)
        if count not in ((1, 4) if mode == "busy" else (4,)):
            parser.error(f"Unsupported scheduler count for the {mode} retirement fixture: {count}")
        name, number = {
            ("busy", 4): ("avm-shutdown-busy", 26),
            ("parked", 4): ("avm-shutdown-parked", 27),
            ("busy", 1): ("avm-shutdown-busy-1", 28),
        }[mode, count]
        if number in retirement_cases:
            parser.error(f"Duplicate retirement fixture: {name}")
        scheduler_cases[number] = count
        retirement_cases[number] = mode
        vm_cases[number] = (name, image, f"{{m3_shutdown,{mode}}}")
        cases.append((name, number))
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
        for name, number in cases:
            if number in vm_cases:
                shutil.copyfile(vm_cases[number][1], images / (name + ".elf"))
                continue
            if number in worker_cases:
                source = {17: "sync.c", 18: "rwlock.c"}.get(number, "workers.c")
                profile = "--native-workers"
                if number in libc_cases:
                    source, profile = "worker-libc.c", "--worker-libc"
                command(["python3", ROOT / "ports/libc/build.py", ROOT / "ports/libc/tests" / source,
                         profile, "--define", f"CASE={number}",
                         "--picolibc-source", args.picolibc_source,
                         "--output", images / (name + ".elf")], name + "-build")
                continue
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
        for name, number in cases:
            image = images / (name + ".elf")
            command([signer, "elf-sign", image, name, key, "service", "1", "1", "0x7", "-"], name + "-sign")
            thread_limit = worker_cases[number][1] if number in worker_cases else scheduler_cases.get(number, 1)
            command([signer, "deployment-sign", output / (name + ".cdep"), name, "fixture/" + name,
                     image, "0", "1", "64" if number in vm_cases else "16",
                     thread_limit, "5000", key], name + "-descriptor")
            if number in retirement_cases:
                command([signer, "deployment-sign", output / (name + "-replacement.cdep"),
                         name, "fixture/" + name, image, "0", "2", "64", thread_limit, "5000", key],
                        name + "-replacement-descriptor")
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
                       "-m", "512M", "-bios", str(firmware), "-cpu", "cortex-a710", "-smp", str(args.smp),
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
                    for name, number in cases:
                        before = len(serial.read_text(errors="replace"))
                        command([signer, "deployment-notify", output / (name + ".cdep"), "127.0.0.1:28081"], name + "-admission", env=environment, timeout=40)
                        if number in retirement_cases:
                            mode = retirement_cases[number]
                            count = scheduler_cases[number]
                            assert json.loads((output / (name + "-admission.txt")).read_text())["generation"] == 1
                            initial = wait_for(lambda s: retirement_instance(s[before:], name, 1, mode, count), 60)
                            asid = retirement_instance(initial[before:], name, 1, mode, count)
                            replacement_start = len(initial)
                            command([signer, "deployment-notify", output / (name + "-replacement.cdep"),
                                     "127.0.0.1:28081"], name + "-replacement-admission",
                                    env=environment, timeout=40)
                            assert json.loads((output / (name + "-replacement-admission.txt")).read_text())["generation"] == 2
                            serial_text = wait_for(
                                lambda s: retirement_instance(s[replacement_start:], name, 2, mode, count), 60)
                            successor = retirement_instance(serial_text[replacement_start:], name, 2, mode, count)
                            retiring = serial_text[replacement_start:].split(
                                f'[agent] launched "{name}" generation=2 asid=', 1)[0]
                            abort = re.search(rf"Aborting user address space {asid}\b", retiring)
                            assert abort, (name, domain_logs(retiring, asid))
                            old_lines = domain_logs(retiring[:abort.start()], asid)
                            assert "[atomvm] runtime=" not in old_lines, name
                            sources = re.findall(r"\[atomvm\] (?:reactor|scheduler) shutdown source=([\w-]+)",
                                                 old_lines)
                            expected_sources = ("cq-wait",) if mode == "parked" else ("nonblocking",)
                            if mode == "busy" and count > 1:
                                expected_sources += ("cq-wait", "private-wait")
                            assert sources and all(source in expected_sources for source in sources), (name, old_lines)
                            assert "[c-runtime] abort" in old_lines and "[c-runtime] exit=" not in old_lines, old_lines
                            if mode == "parked":
                                assert "scheduler entered slot=" not in domain_logs(initial[before:], asid), name
                                assert "scheduler entered slot=" not in old_lines, name
                            record["cases"].append({
                                "name": name, "case": number, "passed": True, "shutdown_sources": sources,
                                "retired_asid": int(asid), "successor_asid": int(successor),
                                "observation": serial_text[before:],
                            })
                            save()
                            continue
                        expected = "CABI expected abort" if number == 7 else "CABI expected nonzero" if number == 6 else "CABI explicit exit" if number == 5 else f"CABI PASS case={number}"
                        if number in vm_cases:
                            expected = vm_cases[number][2]
                        if number in worker_cases:
                            expected = worker_cases[number][2]
                        def observation(text):
                            tail = text[before:]
                            launch = re.search(r'\[agent\] launched "' + re.escape(name) + r'" generation=\d+ asid=(\d+)', tail)
                            if not launch:
                                return ""
                            return "\n".join(line for line in tail.splitlines()
                                             if re.search(r'\[EL0 LOG\] lp=\d+ asid=' + launch[1] + r' ', line))
                        def complete(text):
                            domain_lines = observation(text)
                            terminal = "[c-runtime] abort" if number in abort_cases else f"[c-runtime] exit={17 if number == 6 else 0} "
                            if number == 15:
                                terminal = "[c-runtime] exit with an unjoined worker"
                            if number == 20:
                                terminal = "[worker-libc] libc lock close is unsupported"
                            if expected not in domain_lines or terminal not in domain_lines:
                                return False
                            if number in (15, 20):
                                asid = re.search(r'\[EL0 LOG\] lp=\d+ asid=(\d+) ', domain_lines)[1]
                                return re.search(r'Aborting user address space ' + asid + r'\b', text[before:]) is not None
                            return True
                        serial_text = wait_for(complete, 60)
                        text = observation(serial_text)
                        if number not in (7, 8) and number not in vm_cases and number not in worker_cases:
                            assert "CABI FINI" in text, name
                        if number not in abort_cases:
                            assert "remaining_blocks=0" in text, name
                        if number in (4, 5, 6):
                            assert "released_blocks=1" in text, name
                        if number in (4, 5):
                            assert text.index("CABI ATEXIT") < text.index("CABI FINI"), name
                        if number in (10, idle_case):
                            assert "[atomvm] wake-before-wait passed" in text, name
                            assert "ATOMVM platform=knytos" in text, name
                            assert "ATOMVM PASS elapsed_ms=" in text, name
                            assert "[atomvm] return=ok" in text, name
                            assert re.search(r'\[atomvm\] reactor waits=[1-9]\d* deadlines=[1-9]\d*', text), name
                        if number == 12:
                            for stage in ("values", "processes", "timers", "supervision"):
                                assert f"{{m3,{stage},ok}}" in text, stage
                            assert "{m3,pass}" in text and "[atomvm] return=ok" in text, name
                            reactor = re.search(r'\[atomvm\] reactor waits=(\d+) deadlines=(\d+)', text)
                            assert reactor and int(reactor[2]) >= 6, name
                        if number in scheduler_cases:
                            count = scheduler_cases[number]
                            assert f" schedulers={count} interpreter=yes" in text, name
                            activated = 0 if number == idle_case else count - 1
                            if number != idle_case:
                                assert f"{{schedulers,{count}}}" in text and "{smp,pass}" in text, name
                            assert "[atomvm] return=ok" in text, name
                            assert re.search(r"\[atomvm\] reactor waits=[1-9]\d* deadlines=[1-9]\d*", text), name
                            if count > 1:
                                assert f"[atomvm] schedulers joined={count - 1} activated={activated}" in text, name
                                for slot in range(1, activated + 1):
                                    assert re.search(rf"\[EL0 LOG\] lp={slot} asid=\d+ \[atomvm\] scheduler entered slot={slot}$",
                                                     text, re.MULTILINE), name
                        if number == 14:
                            assert re.search(r'\[EL0 LOG\] lp=1 asid=\d+ WORKER private queue entry', text), name
                            assert "WORKER cross-CPU handshake and admission" in text, name
                        if number == 15:
                            assert "[c-runtime] exit=" not in text, name
                        if number == 16:
                            assert "WORKER finalizer admission fenced" in text, name
                        if number == 17:
                            assert "SYNC one deadline through reacquisition" in text, name
                            assert "SYNC previous slot owner fenced" in text and "released_blocks=1" in text, name
                        if number == 18:
                            for marker in ("shared and nested readers", "writer timeout wakes readers",
                                           "last reader releases writer"):
                                assert "RWLOCK " + marker in text, name
                        if number == 19:
                            assert text.index("WORKER LIBC held record") < text.index("WORKER LIBC worker acquired stream")
                            for role in ("worker", "startup"):
                                for row in range(8):
                                    line = f"WORKER LIBC {role} row={row} text=clear"
                                    assert sum(entry.endswith(line) for entry in text.splitlines()) == 1, line
                        record["cases"].append({"name": name, "case": number, "passed": True,
                                                "observation": serial_text[before:]})
                        save()
                    record["outcome"] = "passed"
                except BaseException:
                    record["snapshot"] = diagnostic.capture(output, {"serial_log": str(serial), "transport": transport, "cpus": args.smp}, "C-runtime-fixture-failure")
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
