#!/usr/bin/env python3
"""Check process-specific config files without changing the launch directory."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from cuzebox_api import CUzeBoxApi
from test_uzenet_room import port


def main():
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="cuzebox-config-") as temporary:
        work = Path(temporary)
        original = b'# Default preferences must not change\nBaud="4800"\n'
        (work / "config.cfg").write_bytes(original)
        (work / "esp.cfg").write_text('Baud="2400"\n', encoding="utf-8")
        processes, clients = [], []
        try:
            for i, baud in enumerate((9600, 115200)):
                config = work / f"player {i+1}.cfg"
                config.write_text(f'Baud="{baud}"\nSerialRoute="1"\nEspEmulationModel="1"\n', encoding="utf-8")
                api_port = port()
                process = subprocess.Popen(
                    [str(exe), "--headless", "--paused", "--config", str(config), "--api-port", str(api_port)],
                    cwd=work, env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"),
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                processes.append(process)
                api = CUzeBoxApi(port=api_port)
                clients.append(api)
                api.wait_ready(20)
                assert api.command("ESP_STATUS")["baud_rate"] == baud
                api.command("RESET")
                assert api.command("ESP_STATUS")["baud_rate"] == baud
            for api in clients:
                api.command("QUIT")
            for process in processes:
                assert process.wait(timeout=10) == 0
            assert (work / "config.cfg").read_bytes() == original
            for i, baud in enumerate((9600, 115200)):
                text = (work / f"player {i+1}.cfg").read_text()
                assert f'Baud="{baud}"' in text
            for arguments in (["--config"], ["--config", ""], ["--config", "x"*256],
                              ["--headless", "--config", str(work / "absent/config.cfg")]):
                result = subprocess.run([str(exe), *arguments], cwd=work, capture_output=True, timeout=10)
                assert result.returncode != 0, arguments
            assert (work / "config.cfg").read_bytes() == original
            print("PASS: concurrent configs, paths with spaces, reset/save isolation, invalid paths")
        finally:
            for api in clients:
                api.close()
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)


if __name__ == "__main__":
    main()
