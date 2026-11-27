#!/usr/bin/env python3
"""Extract the pinned LP64 ERTS time-initialization contract for explicit probes."""

import hashlib
import json
from pathlib import Path
import re
import subprocess

OTP_PIN = "d199a49a01a2079afb6f2c5c9ae047cd4555e68d"


def stage(source, destination):
    destination.mkdir(parents=True, exist_ok=False)
    text = subprocess.check_output(["git", "-C", source, "show",
                                    f"{OTP_PIN}:erts/emulator/beam/sys.h"], text=True)
    pattern = r"typedef struct \{\n    int have_os_monotonic_time;.*?\} ErtsSysInitTimeResult;"
    records = re.findall(pattern, text, re.S)
    if len(records) != 1 or "typedef unsigned long Uint64;" not in text:
        raise SystemExit("Unexpected pinned LP64 time record")
    initializer = re.findall(r"#define ERTS_SYS_INIT_TIME_RESULT_INITER \\\n[^\n]+", text)
    if len(initializer) != 1:
        raise SystemExit("Unexpected pinned time initializer")
    declarations = []
    for name in ("sys_init_time", "erts_late_sys_init_time"):
        found = re.findall(r"^extern void " + name + r"\([^\n]*\);$", text, re.M)
        if len(found) != 1:
            raise SystemExit("Unexpected pinned time declaration: " + name)
        declarations.extend(found)
    header = ('/* Explicit pinned contract probe; not a complete sys.h. */\n'
              '#ifndef KNYTOS_ERTS_TIME_CONTRACT_H\n#define KNYTOS_ERTS_TIME_CONTRACT_H\n'
              '#include "erts_clock_platform.h"\n'
              'typedef unsigned long Uint64;\n' + records[0] + '\n' + initializer[0]
              + '\n' + '\n'.join(declarations) + '\n#endif\n')
    (destination / "erts_time_contract.h").write_text(header)
    result = dict(otp_revision=OTP_PIN, profile="pinned-lp64-time-record-probe",
                  sys_header_sha256=hashlib.sha256(text.encode()).hexdigest(),
                  contract_sha256=hashlib.sha256(header.encode()).hexdigest())
    (destination / "result.json").write_text(json.dumps(result, indent=2) + '\n')
    return result
