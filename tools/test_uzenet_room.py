#!/usr/bin/env python3
"""Two actual AVR cartridges through a temporary local Room TLS/UDP server.

Usage: test_uzenet_room.py EMULATOR UZENET_SOURCE_ROOT
The source root supplies the Room/identity server; it is read only.
"""
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


def run(*args):
    subprocess.run([str(a) for a in args], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def cert(work, name):
    crt, key = work/(name+".crt"), work/(name+".key")
    run("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", key, "-out", crt, "-days", "1", "-subj", "/CN="+name)
    return crt, key


def main():
    exe, source = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="cuzebox-room-") as tmp:
        work = Path(tmp)
        crt, key = cert(work, "localhost")
        relay_port = port()
        run("cc", "-std=gnu11", "-O2", "-pthread",
            f"-DPORT={relay_port}", f"-DUDP_PORT={relay_port}",
            "-DUZENET_TEST_IDENTITY_UID=1", "-DUZENET_TEST_IDENTITY_PERMISSIONS=96",
            f'-DCERT_FILE="{crt}"', f'-DKEY_FILE="{key}"',
            f'-DROOM_SVC_SOCK_PATH="{work / "room.sock"}"',
            f'-DROOM_STATUS_PATH="{work / "room-status.json"}"',
            source/"uzenet-room/uzenet-room-server.c",
            source/"uzenet-identity/uzenet-identity-client.c",
            "-lssl", "-lcrypto", "-o", work/"room")
        run("avr-gcc", "-mmcu=atmega644", "-DF_CPU=28636363UL", "-DNETWORK_PROBE=1",
            "-Os", "-I"+str(ROOT/"third_party/uzenet/include"),
            ROOT/"tests/uzenet/avr_probe.c", ROOT/"third_party/uzenet/src/un_wire.c",
            "-o", work/"probe.elf")
        run("avr-objcopy", "-O", "binary", work/"probe.elf", work/"probe.bin")
        data = (work/"probe.bin").read_bytes()
        header = bytearray(512)
        header[:6] = b"UZEBOX"; header[6] = 1
        struct.pack_into("<I", header, 8, len(data))
        struct.pack_into("<I", header, 334, zlib.crc32(data))
        (work/"probe.uze").write_bytes(header+data)
        processes, apis, logs = [], [], []
        try:
            log = (work/"room.log").open("w+"); logs.append(log)
            processes.append(subprocess.Popen(["stdbuf", "-oL", "-eL", str(work/"room")], stdout=log, stderr=log,
                             env=dict(os.environ, UZENET_UN_RELAY_IPV4="127.0.0.1")))
            for _ in range(100):
                try:
                    with socket.create_connection(("127.0.0.1", relay_port), timeout=.1):
                        break
                except OSError:
                    time.sleep(.05)
            else:
                raise AssertionError("Room startup failed")
            for i in range(2):
                instance = work/str(i); instance.mkdir()
                device_crt, device_key = cert(work, "device"+str(i))
                config = dict(SerialRoute=1, EspEmulationModel=1,
                              EspAtFirmwareProfile=2, Baud=9600, UartProfile=1,
                              WifiMode=1, WifiName="UzenetTest", WifiPass="test",
                              EspAtTranslinkEnabled=1, EspAtTranslinkKind=3,
                              EspAtTranslinkHost="localhost", EspAtTranslinkPort=relay_port,
                              SslAuthMode0=3, SslPkiNum0=0, SslCaNum0=0,
                              SslCaPath0=crt, SslPkiCertPath0=device_crt,
                              SslPkiKeyPath0=device_key)
                (instance/"config.cfg").write_text("ResidentBootloader=0\n"+
                    "".join(f'{k}="{v}"\n' for k, v in config.items()), encoding="utf-8")
                log = (instance/"emulator.log").open("w+"); logs.append(log)
                api_port = port()
                processes.append(subprocess.Popen(["stdbuf", "-oL", "-eL", str(exe), "--headless", "--api-port",
                    str(api_port), str(work/"probe.uze")], cwd=instance,
                    env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"),
                    stdout=log, stderr=log))
                api = CUzeBoxApi(port=api_port); apis.append(api); api.wait_ready(20)
            deadline = time.monotonic()+20
            seen = set()
            while time.monotonic()<deadline:
                states = [api.command("ESP_STATUS") for api in apis]
                key = tuple((s["links"][0]["state"],s["links"][0]["error"]) for s in states)
                if key not in seen:
                    print("TLS states:", key, flush=True); seen.add(key)
                if all(s["links"][0]["open"] and s["cip_mode"] == 1 for s in states):
                    break
                time.sleep(.05)
            else:
                raise AssertionError(f"Saved TLS connection failed: {states}")
            for api in apis:
                api.command("WRITE_MEM SRAM 0x807 1")
            deadline = time.monotonic()+90
            while time.monotonic()<deadline:
                values = [api.command("READ_MEM SRAM 0x800 1")["values"][0] for api in apis]
                if values == [128, 128]:
                    print("PASS: two AVR cartridges, Room TLS, authenticated UDP, 32 exchanges")
                    break
                if any(v >= 0xe0 for v in values):
                    raise AssertionError(f"Cartridge failures: {values}; "+
                        str([api.command("READ_MEM SRAM 0x800 80") for api in apis])+
                        str([api.command("ESP_STATUS") for api in apis]))
                time.sleep(.1)
            else:
                raise AssertionError(f"Timed out: {values}")
        except Exception:
            for proc in processes:
                proc.terminate()
            for proc in processes:
                proc.wait(timeout=10)
            for log in logs:
                log.flush(); log.seek(0); print(log.read()[-4000:])
            raise
        finally:
            for api in apis:
                api.close()
            for proc in processes:
                if proc.poll() is None:
                    proc.terminate(); proc.wait(timeout=10)
            for log in logs:
                log.close()


if __name__ == "__main__":
    main()
