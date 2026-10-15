#!/usr/bin/env python3
"""Exercise source stepping through a private headless emulator instance."""
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

from cuzebox_api import CUzeBoxApi
from test_debug_source import make_elf

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_source_step_api.py <emulator executable>")
    executable = pathlib.Path(sys.argv[1]).resolve()
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="source-step-api-", dir=ROOT / "_obj_") as temp:
        directory = pathlib.Path(temp).resolve()
        assert directory.is_relative_to((ROOT / "_obj_").resolve())
        (directory / "config.cfg").write_text(f"ApiServer=1\nApiServerPort={port}\nGuiPauseWhileOpen=0\n", encoding="utf-8")
        (directory / "fixture.c").write_text("line1\nline2\nline3\nline4\nline5\n", encoding="utf-8")
        make_elf(directory / "fixture.elf")
        (directory / "fixture.hex").write_text(":0200000007C037\n:0A00100000000000000000000000E6\n:00000001FF\n", encoding="ascii")
        env = dict(os.environ, SDL_VIDEODRIVER="invalid-headless-test", SDL_AUDIODRIVER="invalid-headless-test")
        process = subprocess.Popen([str(executable), "--headless", "--paused", "--api-port", str(port)], cwd=directory, env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        client = CUzeBoxApi(port=port)
        try:
            client.wait_ready(15)
            def command(text):
                result = client.command(text)
                assert result.get("ok"), (text, result)
                return result
            def step(mode, expected_pc=None):
                command("SOURCE_STEP " + mode)
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    status = command("RUN_STATUS")
                    if status["paused"]:
                        if expected_pc is not None:
                            assert command("READ_REGS")["pc"] == expected_pc
                        return status
                    time.sleep(.01)
                raise AssertionError("source step failed to pause")
            assert any(c.startswith("SOURCE_STEP") for c in command("HELP")["commands"])
            command("LOAD_ROM WAIT " + str(directory / "fixture.hex"))
            command("LOAD_SYMBOLS AUTO")
            assert command("SOURCE_STATUS")["source_rows"] == 2
            step("INTO", 8)
            assert command("SOURCE_AT PC")["line"] == 1
            step("INTO", 11)
            assert command("SOURCE_AT PC")["line"] == 5
            assert "limit" in step("OVER 32")["status"]
            for text in ["SOURCE_STEP INTO 0", "SOURCE_STEP INTO -1", "SOURCE_STEP INTO 1000001", "SOURCE_STEP INTO 1 extra", "SOURCE_STEP WRONG"]:
                assert not client.command(text).get("ok"), text
            command("CLEAR_SYMBOLS")
            assert not client.command("SOURCE_STEP INTO").get("ok")
            command("QUIT")
            process.wait(timeout=10)
        finally:
            client.close()
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    print("Source stepping API integration: PASS")


if __name__ == "__main__":
    main()
