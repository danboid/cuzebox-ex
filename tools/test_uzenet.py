#!/usr/bin/env python3
"""Build the portable firmware and exercise the emulator's UART/GPIO adapter."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    vendor = ROOT / "third_party/uzenet"
    snapshot = json.loads((vendor / "snapshot.json").read_text())
    for name, digest in snapshot["files"].items():
        assert hashlib.sha256((vendor / name).read_bytes()).hexdigest() == digest, name
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        raise SystemExit("A native C compiler is required")
    with tempfile.TemporaryDirectory(prefix="cuzebox-uzenet-") as tmp:
        work = Path(tmp)
        sources = sorted((vendor / "src").glob("*.c"))
        common = [cc, "-std=gnu17", "-O2", "-Wall", "-Wextra", "-DENABLE_UZENET=1",
                  "-DENABLE_ESP=1", "-I"+str(ROOT), "-I"+str(vendor/"include")]
        sdl = subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()
        for tls in (False, True):
            exe = work / ("adapter-tls" if tls else "adapter")
            subprocess.run(common + sdl + (["-DENABLE_OPENSSL=1"] if tls else []) +
                           [str(ROOT/"tests/uzenet/adapter_regression.c")] +
                           [str(p) for p in sources] + (["-lcrypto", "-lssl"] if tls else []) +
                           ["-o", str(exe)], check=True)
            subprocess.run([str(exe)], cwd=work, check=True)
    print("PASS: pinned firmware hashes and adapter with/without TLS")


if __name__ == "__main__":
    main()
