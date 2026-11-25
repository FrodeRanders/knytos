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
GUEST_MAC = "52:54:00:12:34:56"
CASES = [("c-hello", 0), ("c-memory", 1), ("c-format", 2), ("c-clock", 3),
         ("c-startup", 4), ("c-exit", 5), ("c-error", 6), ("c-abort", 7), ("c-empty", 8),
         ("c-shutdown-without-request", 9), ("c-survivor", 0)]
HOLD_SERVICE = "m4-hold-service"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def domain_logs(text, asid):
    prefix = re.compile(rf"\[EL0 LOG\] lp=\d+ asid={asid} ")
    return "\n".join(line for line in text.splitlines() if prefix.search(line))


def verify_active_cancel(text, server_asid, client_asid):
    stages = [
        (server_asid, "[m4 hold] accepted case=1 payload=true"),
        (client_asid, "{m4,cancelling_accepted_call}"),
        (server_asid, "[m4 hold] late reply rejected case=1"),
        (client_asid, "{m4,accepted_cancel_reused_ms,"),
        (server_asid, "[m4 hold] accepted case=2 payload=true"),
        (server_asid, "[m4 hold] late reply rejected case=2"),
        (client_asid, "{m4,accepted_call_timed_out}"),
    ]
    for case in (3, 4):
        stages.extend((
            (server_asid, f"[m4 hold] accepted case={case}"),
            (client_asid, f"{{m4,releasing_request_owner,{case}}}"),
            (server_asid, f"[m4 hold] late reply rejected case={case}"),
            (client_asid, f"{{m4,owner_exit_reused_ms,{case},"),
        ))
    stages.extend((
        (client_asid, "{m4,byte_payload_roundtrip}"),
        (client_asid, "{m4,byte_payload_bounds}"),
    ))
    offset = 0
    for asid, marker in stages:
        match = re.search(rf"\[EL0 LOG\] lp=\d+ asid={asid} {re.escape(marker)}", text[offset:])
        assert match is not None, marker
        offset += match.end()


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


def verify_vm_shutdown(text, name, asid, mode, count, reason, service_worker=False):
    acknowledgements = list(re.finditer(
        rf"\[supervisor\] {reason} shutdown acknowledged asid={asid} generation=(\d+)\b", text))
    assert len(acknowledgements) == 1, (name, domain_logs(text, asid))
    acknowledgement = acknowledgements[0]
    old_lines = domain_logs(text[:acknowledgement.start()], asid)
    assert "[atomvm] runtime=" not in old_lines, name
    source = "cq-wait" if mode == "parked" else "nonblocking"
    if mode == "busy" and (count > 1 or service_worker) and "shutdown source=cq-wait" in old_lines:
        source = "cq-wait"
    stages = [f"[atomvm] reactor shutdown source={source}",
              "[atomvm] schedulers stopped for shutdown", "[c-runtime] exit=0 "]
    if count > 1:
        activated = count - 1 if mode == "busy" else 0
        stages.insert(1, f"[atomvm] schedulers joined={count - 1} activated={activated}")
    if service_worker:
        stages.insert(1, "[atomvm] service worker joined")
    assert all(stage in old_lines for stage in stages), (name, old_lines)
    assert [old_lines.index(stage) for stage in stages] == sorted(
        old_lines.index(stage) for stage in stages), (name, old_lines)
    assert "remaining_blocks=0" in old_lines and "[c-runtime] abort" not in old_lines, old_lines
    assert not re.search(rf"Aborting user address space {asid}\b", text), name
    return {"shutdown_source": source, "acknowledged_root_generation": int(acknowledgement[1]),
            "retired_asid": int(asid)}


def vm_metrics(tls, reader, asid, count):
    response, _ = tls.exchange(
        reader, 28080, b"GET /metrics HTTP/1.1\r\nHost: localhost\r\n\r\n",
        200, 3, include_body=True)
    report = json.loads(response["body"])["threads"]
    rows = sorted((row for row in report["sample"] if row["asid"] == asid),
                  key=lambda row: (row["tid"], row["generation"]))
    domain, = [entry for entry in report["domains"] if entry["asid"] == asid]
    assert len(rows) == count == domain["threads"] and all(not row["saturated"] for row in rows), rows
    assert report["freq_hz"] > 0 and domain["heap_valid"] == 1, (report["freq_hz"], domain)
    return {"ticks": report["mono_ticks"], "frequency_hz": report["freq_hz"],
            "threads": rows, "domain": domain}


def performance_result(lines, count, windows):
    assert f"{{perf,schedulers,{count}}}" in lines and "{perf,pass}" in lines, lines
    assert re.search(r"\[c-runtime\] exit=0 .*remaining_blocks=0", lines), lines
    for slot in range(1, count):
        assert re.search(rf"\[EL0 LOG\] lp={slot} asid=\d+ \[atomvm\] scheduler entered slot={slot}$",
                         lines, re.MULTILINE), lines
    if count > 1:
        assert f"[atomvm] schedulers joined={count - 1} activated={count - 1}" in lines, lines
    throughput = [tuple(map(int, match)) for match in
                  re.findall(r"\{perf,throughput,(\d+),(\d+),(\d+)\}", lines)]
    latency = [tuple(map(int, match)) for match in
               re.findall(r"\{perf,latency,(\d+),(\d+),(\d+)\}", lines)]
    assert [trial for trial, _, _ in throughput] == [1, 2, 3], throughput
    assert all(messages == 96000 and elapsed > 0 for _, messages, elapsed in throughput), throughput
    assert [sample for sample, _, _ in latency] == list(range(1, 33)), latency
    for name, (first, last) in windows.items():
        start, = map(int, re.findall(rf"\{{perf,{name}_start,(\d+)\}}", lines))
        end, = map(int, re.findall(rf"\{{perf,{name}_end,(\d+)(?:,\d+)?\}}", lines))
        frequency = first["frequency_hz"]
        elapsed = last["ticks"] - first["ticks"]
        assert frequency == last["frequency_hz"] and elapsed >= frequency, (name, windows[name])
        assert end - start >= 8000000, (name, start, end)
        assert start <= first["ticks"] * 1000000 // frequency < last["ticks"] * 1000000 // frequency <= end
        slices = []
        for old, new in zip(first["threads"], last["threads"], strict=True):
            assert (old["tid"], old["generation"]) == (new["tid"], new["generation"]), (old, new)
            if name == "idle":
                assert old["state"] == new["state"] == "blocked", (old, new)
            delta = new["runtime_ticks"] - old["runtime_ticks"]
            assert delta >= 0, (old, new)
            slices.append(delta)
        last["elapsed_ticks"] = elapsed
        last["runtime_delta_ticks"] = slices
    return {"throughput_trials": throughput, "timer_and_wake_samples": latency,
            "cpu_windows": windows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--picolibc-source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--atomvm-elf", type=Path)
    parser.add_argument("--atomvm-utc-elf", type=Path)
    parser.add_argument("--atomvm-semantics-elf", type=Path)
    parser.add_argument("--atomvm-overflow-elf", type=Path)
    parser.add_argument("--atomvm-service-elf", type=Path,
                        help="run the scoped native-service fixture with signed test grants")
    parser.add_argument("--atomvm-active-cancel-elf", type=Path,
                        help="qualify cancellation against a signed service that holds replies")
    parser.add_argument("--atomvm-scheduler-elf", action="append", type=Path, default=[],
                        help="add an AtomVM scheduler workload using its adjacent build manifest")
    parser.add_argument("--atomvm-idle-scheduler-elf", type=Path,
                        help="add the AtomVM hello fixture with prepared but unused workers")
    parser.add_argument("--atomvm-performance-elf", action="append", type=Path, default=[],
                        help="measure signed AtomVM message, timer and CPU performance")
    parser.add_argument("--atomvm-shutdown-busy-elf", action="append", type=Path, default=[],
                        help="retire an AtomVM VM running continuous Erlang work (one or four schedulers)")
    parser.add_argument("--atomvm-shutdown-parked-elf", type=Path,
                        help="retire an AtomVM VM with unused native workers and no timers")
    parser.add_argument("--atomvm-node-shutdown", action="store_true",
                        help="drain all supplied AtomVM shutdown fixtures using one signed node shutdown")
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
    def scheduler_manifest(image):
        manifest = json.loads(image.with_suffix(".atomvm.json").read_text())
        count = manifest["schedulers"]
        if count not in (1, 2, 4) or count > args.smp or digest(image) != manifest["unsigned_elf_sha256"]:
            parser.error(f"Scheduler image does not match its manifest or guest CPU count: {image}")
        if manifest.get("service_worker") and (count != 1 or args.smp < 2):
            parser.error("The service worker requires one Erlang scheduler and two guest CPUs")
        return manifest
    service_case = 35
    active_cancel_case = 36
    service_workers = set()
    if args.atomvm_service_elf:
        if not scheduler_manifest(args.atomvm_service_elf).get("service_worker"):
            parser.error("--atomvm-service-elf requires a service worker image")
        service_workers.add(service_case)
        vm_cases[service_case] = ("avm-native-service", args.atomvm_service_elf, "{m4,native_service,pass}")
    if args.atomvm_active_cancel_elf:
        if not scheduler_manifest(args.atomvm_active_cancel_elf).get("service_worker"):
            parser.error("--atomvm-active-cancel-elf requires a service worker image")
        service_workers.add(active_cancel_case)
        vm_cases[active_cancel_case] = ("avm-active-cancel", args.atomvm_active_cancel_elf,
                                       "{m4,active_cancel,pass}")
    scheduler_cases = {}
    for image in args.atomvm_scheduler_elf:
        count = scheduler_manifest(image)["schedulers"]
        number = 20 + count
        if number in scheduler_cases:
            parser.error(f"Duplicate scheduler count: {count}")
        scheduler_cases[number] = count
        vm_cases[number] = (f"avm-schedulers-{count}", image, "[atomvm] completed status=0")
    idle_case = 25
    if args.atomvm_idle_scheduler_elf:
        count = scheduler_manifest(args.atomvm_idle_scheduler_elf)["schedulers"]
        if count == 1:
            parser.error("The idle scheduler fixture requires worker threads")
        scheduler_cases[idle_case] = count
        vm_cases[idle_case] = ("avm-idle-schedulers", args.atomvm_idle_scheduler_elf, "[atomvm] completed status=0")
    performance_cases = {}
    for image in args.atomvm_performance_elf:
        count = scheduler_manifest(image)["schedulers"]
        number = 30 + count
        if number in performance_cases:
            parser.error(f"Duplicate performance fixture for {count} schedulers")
        scheduler_cases[number] = performance_cases[number] = count
        vm_cases[number] = (f"avm-performance-{count}", image, "[atomvm] completed status=0")
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
    abort_cases = (7, 9, 11, 13, 15, 20)
    cases += CASES[-1:]
    retirement_cases = {}
    retirement_images = [("parked", args.atomvm_shutdown_parked_elf)]
    retirement_images.extend(("busy", path) for path in args.atomvm_shutdown_busy_elf)
    for mode, image in retirement_images:
        if image is None:
            continue
        manifest = scheduler_manifest(image)
        count = manifest["schedulers"]
        if count not in ((1, 4) if mode == "busy" else (4,)):
            parser.error(f"Unsupported scheduler count for the {mode} retirement fixture: {count}")
        name, number = {
            ("busy", 4): ("avm-shutdown-busy", 26),
            ("parked", 4): ("avm-shutdown-parked", 27),
            ("busy", 1): ("avm-shutdown-busy-1", 28),
        }[mode, count]
        if number in retirement_cases:
            parser.error(f"Duplicate retirement fixture: {name}")
        if manifest.get("service_worker"):
            service_workers.add(number)
        scheduler_cases[number] = count
        retirement_cases[number] = mode
        vm_cases[number] = (name, image, f"{{m3_shutdown,{mode}}}")
        cases.append((name, number))
    if args.atomvm_node_shutdown and not retirement_cases:
        parser.error("--atomvm-node-shutdown requires at least one AtomVM shutdown fixture")
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
        if args.atomvm_active_cancel_elf:
            service_target = ROOT / "crates/knytos-services/target"
            command(["cargo", "build", "--locked", "-p", "knytos-app", "--features", "test-support",
                     "--example", "atomvm_cancel_fixture", "--target", "crates/knytos-services/aarch64-unknown-none.json",
                     "--target-dir", service_target, "--release", "-Z", "build-std=core,alloc"],
                    HOLD_SERVICE + "-build")
            hold_image = images / (HOLD_SERVICE + ".elf")
            shutil.copyfile(service_target / "aarch64-unknown-none/release/examples/atomvm_cancel_fixture", hold_image)
            command([signer, "elf-sign", hold_image, HOLD_SERVICE, key, "service", "1", "1", "0x7", "-"],
                    HOLD_SERVICE + "-sign")
            command([signer, "deployment-sign", output / (HOLD_SERVICE + ".cdep"), HOLD_SERVICE,
                     "fixture/" + HOLD_SERVICE, hold_image, "0", "1", "16", "1", "5000", key,
                     "m4.hold=publish"], HOLD_SERVICE + "-descriptor")
        for name, number in cases:
            image = images / (name + ".elf")
            command([signer, "elf-sign", image, name, key, "service", "1", "1", "0x7", "-"], name + "-sign")
            thread_limit = (2 if number in service_workers else
                            worker_cases[number][1] if number in worker_cases else scheduler_cases.get(number, 1))
            grants = ["m4.missing=call"] if number in service_workers and number != active_cancel_case else []
            if number == service_case:
                grants.extend(("time=call", "observe=call"))
            if number == active_cancel_case:
                grants.append("m4.hold=call")
            if number in retirement_cases and number in service_workers:
                grants.append("time=call")
            command([signer, "deployment-sign", output / (name + ".cdep"), name, "fixture/" + name,
                     image, "0", "1", "64" if number in vm_cases else "16",
                     thread_limit, "5000", key, *grants], name + "-descriptor")
            if number in retirement_cases and not args.atomvm_node_shutdown:
                command([signer, "deployment-sign", output / (name + "-replacement.cdep"),
                         name, "fixture/" + name, image, "0", "2", "64", thread_limit, "5000", key, *grants],
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
                           KNYTOS_MANAGEMENT_READER_CERT_FILE=str(credentials / "reader.pem"),
                           KNYTOS_MANAGEMENT_READER_KEY_FILE=str(credentials / "reader.key"),
                           KNYTOS_MANAGEMENT_SERVER_NAME="127.0.0.1")
        if performance_cases:
            tls_spec = importlib.util.spec_from_file_location(
                "management_tls", ROOT / "scripts/tests/management_tls.py")
            tls = importlib.util.module_from_spec(tls_spec)
            tls_spec.loader.exec_module(tls)
            reader = tls.context("reader", environment)
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
                       "-device", f"virtio-net-pci,netdev=knytos-net,disable-legacy=on,iommu_platform=on,mac={GUEST_MAC},addr=1,romfile=",
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
                    live = []
                    for name, number in cases:
                        if number == active_cancel_case:
                            command([signer, "deployment-notify", output / (HOLD_SERVICE + ".cdep"),
                                     "127.0.0.1:28081"], HOLD_SERVICE + "-admission", env=environment, timeout=40)
                            launch = re.compile(rf'\[agent\] launched "{HOLD_SERVICE}" generation=1 asid=(\d+)')
                            started = wait_for(lambda s: launch.search(s) and
                                               "[m4 hold] published" in domain_logs(s, launch.search(s)[1]), 60)
                            hold_asid = int(launch.search(started)[1])
                        before = len(serial.read_text(errors="replace"))
                        command([signer, "deployment-notify", output / (name + ".cdep"), "127.0.0.1:28081"], name + "-admission", env=environment, timeout=40)
                        if number in performance_cases:
                            count = performance_cases[number]
                            launch = re.compile(rf'\[agent\] launched "{re.escape(name)}" generation=1 asid=(\d+)')
                            started = wait_for(lambda s: launch.search(s[before:]), 60)
                            asid = int(launch.search(started[before:])[1])
                            windows = {}
                            for phase in ("idle", "load"):
                                wait_for(lambda s: f"{{perf,{phase}_start," in domain_logs(s[before:], asid), 60)
                                first = vm_metrics(tls, reader, asid, count)
                                time.sleep(1.25)  # Space the two observations within the guest phase.
                                last = vm_metrics(tls, reader, asid, count)
                                windows[phase] = (first, last)
                                (output / (name + "-metrics.json")).write_text(json.dumps(windows, indent=2) + "\n")
                            finished = wait_for(lambda s: "[c-runtime] exit=" in domain_logs(s[before:], asid), 60)
                            result = performance_result(domain_logs(finished[before:], asid), count, windows)
                            record["cases"].append({"name": name, "case": number, "passed": True,
                                                    "asid": asid, "performance": result,
                                                    "observation": finished[before:]})
                            save()
                            continue
                        if number in retirement_cases:
                            mode = retirement_cases[number]
                            count = scheduler_cases[number]
                            assert json.loads((output / (name + "-admission.txt")).read_text())["generation"] == 1
                            initial = wait_for(lambda s: retirement_instance(s[before:], name, 1, mode, count), 60)
                            asid = retirement_instance(initial[before:], name, 1, mode, count)
                            if mode == "parked":
                                assert "scheduler entered slot=" not in domain_logs(initial[before:], asid), name
                            if args.atomvm_node_shutdown:
                                live.append((name, number, asid, before))
                                continue
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
                            checked = verify_vm_shutdown(retiring, name, asid, mode, count, "deployment",
                                                         number in service_workers)
                            record["cases"].append({
                                "name": name, "case": number, "passed": True, **checked,
                                "successor_asid": int(successor),
                                "observation": serial_text[before:],
                            })
                            save()
                            continue
                        expected = "CABI expected abort" if number == 7 else "CABI expected nonzero" if number == 6 else "CABI explicit exit" if number == 5 else f"CABI PASS case={number}"
                        if number == 9:
                            expected = "CABI shutdown without request"
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
                        if number not in (7, 8, 9) and number not in vm_cases and number not in worker_cases:
                            assert "CABI FINI" in text, name
                        if number == 9:
                            assert "[c-runtime] shutdown completion without a request" in text, name
                            assert "CABI FINI" not in text, name
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
                        if number == service_case:
                            for stage in ("scalar_reply", "unauthorized", "attachment_rejected",
                                          "timer_with_pending_call", "abandoned_slot_reused",
                                          "foreign_cancel_rejected", "cancelled_reply_suppressed",
                                          "cancelled_slot_reused", "reusable_resource_is_scoped",
                                          "closed_resource_is_stale", "resource_gc_reclaimed_slot",
                                          "resource_owner_exit_reclaimed_slot"):
                                assert f"{{m4,{stage}}}" in text, stage
                            assert "[atomvm] service worker joined" in text and "[atomvm] return=ok" in text, name
                        if number == active_cancel_case:
                            client_asid = int(re.search(r"\[EL0 LOG\] lp=\d+ asid=(\d+)", text)[1])
                            verify_active_cancel(serial_text[before:], hold_asid, client_asid)
                            assert "[atomvm] service worker joined" in text and "[atomvm] return=ok" in text, name
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
                    if args.atomvm_node_shutdown:
                        drain = ROOT / "scripts/tests/test-management-drain.py"
                        command([signer, "node-key", GUEST_MAC], "shutdown-node-key")
                        node = (output / "shutdown-node-key.txt").read_text().strip()
                        now = int(time.time())
                        intent = output / "node.cshutdown"
                        command([signer, "shutdown-sign", intent, now, node, now, now + 300,
                                 60000, 5000, key], "shutdown-sign")
                        initial = serial.read_text(errors="replace")
                        for name, _, asid, before in live:
                            assert "[c-runtime] exit=" not in domain_logs(initial[before:], asid), name
                        qmp = diagnostic.Qmp(transport + "/qmp.sock")
                        try:
                            deadline = time.monotonic() + 90
                            command(["python3", drain, "submit", "--intent", intent,
                                     "--client-tool", signer, "--status-port", 28080,
                                     "--deployment-port", 28081, "--serial-log", serial],
                                    "shutdown-admission", env=environment, timeout=50)
                            event = qmp.receive(deadline)
                            while event.get("event") != "SHUTDOWN":
                                event = qmp.receive(deadline)
                            assert event["data"]["guest"] is True and event["data"]["reason"] == "guest-shutdown", event
                            assert process.wait(timeout=max(0, deadline - time.monotonic())) == 0
                        finally:
                            qmp.close()
                        command(["python3", drain, "verify", serial], "shutdown-ingress-verify")
                        final = serial.read_text(errors="replace")
                        stopping = final[len(initial):]
                        ordered = ["[agent] node shutdown requested deadline_ms=",
                                   "[agent] application domains drained; draining connectors",
                                   "[agent] child domains drained for node shutdown",
                                   "[shutdown] DeploymentAgent: acknowledged=1 unacknowledged=0 forced=0 lifecycle_status=2",
                                   "[shutdown] POWER-OFF REQUESTED via PSCI"]
                        positions = [stopping.index(stage) for stage in ordered]
                        assert positions == sorted(positions), positions
                        vm_drain = stopping[positions[0]:positions[1]]
                        for name, number, asid, before in live:
                            checked = verify_vm_shutdown(vm_drain, name, asid, retirement_cases[number],
                                                         scheduler_cases[number], "node", number in service_workers)
                            record["cases"].append({"name": name, "case": number, "passed": True,
                                                    **checked, "observation": final[before:]})
                        admitted = [json.loads(line)["generation"]
                                    for line in (output / "shutdown-admission.txt").read_text().splitlines()
                                    if line.startswith("{")]
                        assert admitted == [1], admitted
                        record["node_shutdown"] = {"admitted_generation": admitted[0], "target_node": node,
                                                   "intent_sha256": digest(intent), "qmp_event": event,
                                                   "qemu_exit_status": process.returncode}
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
