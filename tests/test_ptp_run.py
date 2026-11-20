#!/usr/bin/env python3
"""Offline tests only. Never opens serial/Bluetooth or executes board firmware."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
scratch = Path(os.environ.get("TMPDIR", tempfile.gettempdir()))
with tempfile.TemporaryDirectory(prefix="ptp-tests-", dir=scratch) as directory:
    for name in ("core", "descriptor", "protocol", "link", "input"):
        exe = Path(directory) / name
        command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-Itests/fakes", f"tests/test_ptp_{name}.cpp", "-o", str(exe)]
        print("+", " ".join(command), flush=True)
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([str(exe)], cwd=root, check=True)
print("PASS: 5 offline executables; ASan + UBSan; no hardware access")
