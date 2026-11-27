"""Stage the explicit, incomplete Knytos selection in pinned OTP headers."""

import hashlib
from pathlib import Path
import shutil
import subprocess

from configure import OTP_PIN

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage_headers(source, output):
    tree = output / "otp"
    tree.mkdir()
    command = ["git", "-C", str(source), "archive", OTP_PIN, "erts/include/internal"]
    with subprocess.Popen(command, stdout=subprocess.PIPE) as archive:
        subprocess.run(["tar", "-xf", "-", "-C", str(tree)], stdin=archive.stdout, check=True)
        archive.stdout.close()
        if status := archive.wait():
            raise subprocess.CalledProcessError(status, command)
    include = tree / "erts/include/internal"
    patch = ROOT / "ports/erts/otp-29.1.2-ethread.patch"
    original = digest(include / "ethread.h")
    subprocess.run(["patch", "-p1", "--batch", "--fuzz=0", "--input", str(patch)],
                   cwd=tree, check=True)
    (include / "knytos").mkdir()
    headers = sorted((ROOT / "ports/erts").glob("*.h"))
    for header in headers:
        shutil.copy2(header, include / "knytos" / header.name)
    # This is a compile-probe profile, not an Autoconf output or a claim that
    # full initialization exists. Sizes come from the target compiler; cache
    # line size is OTP's existing 64-byte assumption in ethread.h.
    profile = output / "header-profile.h"
    profile.write_text("\n".join((
        "#define ETHR_HAVE_ETHREAD_DEFINES 1",
        "#define ETHR_KNYTOS 1",
        "#define ETHR_SIZEOF_INT __SIZEOF_INT__",
        "#define ETHR_SIZEOF_LONG __SIZEOF_LONG__",
        "#define ETHR_SIZEOF_LONG_LONG __SIZEOF_LONG_LONG__",
        "#define ETHR_SIZEOF_PTR __SIZEOF_POINTER__",
        "#define ASSUMED_CACHE_LINE_SIZE 64",
        "",
    )))
    inputs = [patch, Path(__file__).resolve(), *headers]
    evidence = {
        "otp_revision": OTP_PIN,
        "original_ethread_sha256": original,
        "selected_ethread_sha256": digest(include / "ethread.h"),
        "profile_sha256": digest(profile),
        "inputs_sha256": {str(path.relative_to(ROOT)): digest(path) for path in inputs},
    }
    return include, profile, evidence
