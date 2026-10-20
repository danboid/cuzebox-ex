#!/usr/bin/env python3
"""Linux/WSL regression for archive layout and standalone/PortMaster launch."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile


def main():
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="cuzebox-portmaster-") as temporary:
        work = Path(temporary)
        binary = work / "cuzebox"
        binary.write_text(
            '#!/bin/bash\nprintf "%s\\n" "$PWD" "$@" '
            '"${SDL_GAMECONTROLLERCONFIG:-}" > "$TEST_LOG"\nexit 7\n'
        )
        binary.chmod(0o755)
        archive = work / "package.zip"
        subprocess.run(
            ["make", "TSYS=linux", "-o", str(binary), f"OUT={binary}",
             f"PORTMASTER_STAGE_ROOT={work / 'stage'}",
             f"PORTMASTER_OUT_ZIP={archive}", "package-portmaster"],
            cwd=root, check=True,
        )
        with zipfile.ZipFile(archive) as package:
            names = set(package.namelist())
            assert {"CUzeBox.sh", "CUzeBox/cuzebox", "CUzeBox/config.cfg",
                    "port.json", "README-portmaster.md"} <= names
            metadata = json.loads(package.read("port.json"))
            assert all(item in names for item in metadata["items"])
            assert package.getinfo("CUzeBox.sh").external_attr >> 16 & 0o111
            package.extractall(work / "installed ports")
        installed = work / "installed ports"
        (installed / "CUzeBox/cuzebox").chmod(0o755)
        launcher = installed / "CUzeBox.sh"
        subprocess.run(["bash", "-n", launcher], check=True)
        env = dict(os.environ, XDG_DATA_HOME=str(work / "xdg"),
                   TEST_LOG=str(work / "launch.log"))
        result = subprocess.run(["bash", launcher, "a rom.uze", "--test"], env=env)
        assert result.returncode == 7
        lines = (work / "launch.log").read_text().splitlines()
        assert lines[:3] == [str(installed / "CUzeBox"), "a rom.uze", "--test"]
        control = work / "xdg/PortMaster"
        control.mkdir(parents=True)
        (control / "control.txt").write_text(
            'CFW_NAME=test\n'
            'get_controls() { sdl_controllerconfig="test-controller"; }\n'
            'pm_platform_helper() { printf "setup\\n" >> "$TEST_HOOKS"; }\n'
            'pm_finish() { printf "finish\\n" >> "$TEST_HOOKS"; }\n'
        )
        env["TEST_HOOKS"] = str(work / "hooks.log")
        result = subprocess.run(["bash", launcher, "a rom.uze"], env=env)
        assert result.returncode == 7
        assert (work / "launch.log").read_text().splitlines()[-1] == "test-controller"
        assert (work / "hooks.log").read_text().splitlines() == ["setup", "finish"]
    print("PASS: PortMaster archive, arguments, controls, cleanup and exit status")


if __name__ == "__main__":
    main()
