#!/usr/bin/env python3
"""Actual AVR cartridge test for the optional Uzenet firmware profile."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from cuzebox_api import CUzeBoxApi

ROOT = Path(__file__).resolve().parents[1]


def main():
    exe = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="cuzebox-uzenet-avr-") as tmp:
        work = Path(tmp)
        subprocess.run(["avr-gcc", "-mmcu=atmega644", "-DF_CPU=28636363UL", "-Os",
                        "-I"+str(ROOT/"third_party/uzenet/include"),
                        str(ROOT/"tests/uzenet/avr_probe.c"),
                        str(ROOT/"third_party/uzenet/src/un_wire.c"),
                        "-o", str(work/"probe.elf")], check=True)
        subprocess.run(["avr-objcopy", "-O", "binary", str(work/"probe.elf"),
                        str(work/"probe.bin")], check=True)
        data = (work/"probe.bin").read_bytes()
        header = bytearray(512)
        header[:6] = b"UZEBOX"; header[6] = 1
        struct.pack_into("<I", header, 8, len(data))
        struct.pack_into("<I", header, 334, zlib.crc32(data))
        (work/"probe.uze").write_bytes(header+data)
        (work/"config.cfg").write_text(
            'ResidentBootloader=0\nSerialRoute="1"\nEspEmulationModel="1"\n'
            'EspAtFirmwareProfile="2"\nBaud="9600"\nUartProfile="1"\n')
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0)); port = sock.getsockname()[1]
        with (work/"emulator.log").open("w+") as log:
            proc = subprocess.Popen([exe, "--headless", "--unthrottled", "--api-port",
                                     str(port), str(work/"probe.uze")], cwd=work,
                                    env=dict(os.environ, SDL_VIDEODRIVER="dummy",
                                             SDL_AUDIODRIVER="dummy"),
                                    stdout=log, stderr=log)
            api = CUzeBoxApi(port=port)
            try:
                api.wait_ready(20)
                assert api.command("SERIAL_STATUS")["at_firmware"] == 2
                deadline = time.monotonic()+30
                while time.monotonic()<deadline:
                    result = api.command("READ_MEM SRAM 0x800 1")
                    value = result["values"][0]
                    if value == 0x80:
                        print("PASS: AVR PA7 entry, UART v4 services, EXIT and stock AT")
                        break
                    if value >= 0xe0:
                        raise AssertionError(f"AVR probe failed: {value:#x}; "
                                             f"{api.command('READ_MEM SRAM 0x800 80')}; "
                                             f"{api.command('SERIAL_STATUS')}")
                    time.sleep(.1)
                else:
                    raise AssertionError(f"AVR probe timed out: {result}")
                assert api.command("SERIAL_SET AT_FIRMWARE 1")["ok"]
                assert api.command("SERIAL_SET AT_FIRMWARE 2")["ok"]
                assert api.command("CONFIG SAVE")["ok"]
                assert api.command("CONFIG RELOAD")["ok"]
                assert api.command("SERIAL_STATUS")["at_firmware"] == 2
            except Exception:
                log.flush();log.seek(0);print(log.read()[-8000:])
                raise
            finally:
                api.close();proc.terminate();proc.wait(timeout=10)


if __name__ == "__main__":
    main()
