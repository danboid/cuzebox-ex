"""Verify RGB30 signal shutdown using a Linux test build with API/debugger enabled."""
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile

from cuzebox_api import CUzeBoxApi


def main():
    binary = Path(sys.argv[1]).resolve()
    for shutdown_signal in (signal.SIGTERM, signal.SIGINT):
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        with tempfile.TemporaryDirectory(prefix="cuzebox-shutdown-") as temp:
            directory = Path(temp)
            cfg = directory / "config.cfg"
            cfg.write_text("ApiServer=0\nSystemMessages=1\n")
            process = subprocess.Popen(
                [str(binary), "--headless", "--paused", "--api-port", str(port)],
                cwd=directory, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                env=dict(os.environ, SDL_VIDEODRIVER="invalid-handheld-test",
                         SDL_AUDIODRIVER="invalid-handheld-test"))
            client = CUzeBoxApi(port=port)
            try:
                client.wait_ready(15)
                result = client.command("EMU_SET system_messages 0")
                assert result.get("ok"), result
                result = client.command("WRITE_MEM EEPROM 0 90")
                assert result.get("ok"), result
                process.send_signal(shutdown_signal)
                process.wait(timeout=10)
                assert process.returncode == 0, process.returncode
                assert "SystemMessages=0" in cfg.read_text(), "preference not saved"
                assert (directory / "eeprom.bin").read_bytes()[0] == 90, "EEPROM not saved"
            finally:
                client.close()
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
    print("Handheld SIGTERM/SIGINT save flushing: PASS")


if __name__ == "__main__":
    main()
