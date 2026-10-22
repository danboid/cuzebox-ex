#!/usr/bin/env python3
"""Run UNTool's ROM uploader through the emulator's AVR GPIO/UART."""
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from cuzebox_api import CUzeBoxApi

ROOT=Path(__file__).resolve().parents[1]
exe=str(Path(sys.argv[1]).resolve())
untool=Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory(prefix="esp-rom-avr-") as directory:
    work=Path(directory)
    for name in ("uzebox.h","pff.h","spiram.h"):
        shutil.copyfile(untool/"tests/fake"/name,work/name)
    subprocess.run(["avr-gcc","-mmcu=atmega644","-DF_CPU=28636363UL","-Os","-std=gnu99",
                    "-ffunction-sections","-fdata-sections","-I"+str(work),"-I"+str(untool),
                    str(ROOT/"tests/esp_at/rom_loader_avr.c"),str(untool/"esp_rom.c"),
                    "-Wl,--gc-sections","-o",str(work/"probe.elf")],check=True)
    subprocess.run(["avr-objcopy","-O","binary",str(work/"probe.elf"),str(work/"probe.bin")],check=True)
    program=(work/"probe.bin").read_bytes();header=bytearray(512)
    header[:6]=b"UZEBOX";header[6]=1
    struct.pack_into("<I",header,8,len(program));struct.pack_into("<I",header,334,zlib.crc32(program))
    (work/"probe.uze").write_bytes(header+program)
    (work/"config.cfg").write_text('ResidentBootloader=0\nSerialRoute="1"\nEspEmulationModel="1"\nEspAtFirmwareProfile="2"\nBaud="9600"\nUartProfile="1"\n')
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0));port=sock.getsockname()[1]
    with (work/"emulator.log").open("w+") as log:
        proc=subprocess.Popen([exe,"--headless","--unthrottled","--api-port",str(port),str(work/"probe.uze")],
                              cwd=work,env=dict(os.environ,SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy"),stdout=log,stderr=log)
        api=CUzeBoxApi(port=port)
        try:
            api.wait_ready(20);deadline=time.monotonic()+45
            while time.monotonic()<deadline:
                status=api.command("READ_MEM SRAM 0x800 1")["values"][0]
                if status==0x80:break
                if status>=0xe0:raise AssertionError(f"AVR failure {status:#x}: {api.command('READ_MEM SRAM 0x800 80')}; {api.command('SERIAL_STATUS')}")
                time.sleep(.1)
            else:raise AssertionError(f"AVR timeout {status:#x}")
            flash=(work/"esp-flash.bin").read_bytes()
            assert len(flash)==16+4*1024*1024 and flash[12]==1
            assert flash[16:2064]==bytes(range(256))*8
            print("PASS: real AVR PA7/PD3 reset, UART autobaud, ROM sync/RDID, flash writes and interrupted recovery")
        except Exception:
            log.flush();log.seek(0);print(log.read()[-5000:]);raise
        finally:
            api.close();proc.terminate();proc.wait(timeout=10)
