#!/usr/bin/env python3
"""Record/replay a self-contained movie through the headless API."""
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

def raw_compact(logical):
    result = bytearray(b"CZINPUT\0" + struct.pack("<II", 4, 16))
    for start in range(0, len(logical), 65536):
        block = logical[start:start+65536]
        result.extend(struct.pack("<4sIII", b"CMP2", len(block), len(block), zlib.crc32(block)))
        result.extend(block)
    return bytes(result)

def expand_compact(data):
    """Independent decoder/oracle for the documented v2 block layout."""
    expanded = bytearray(data[:16]); struct.pack_into("<I", expanded, 8, 3)
    offset = 16
    while offset < len(data):
        tag, raw_size, stored_size, checksum = struct.unpack_from("<4sIII", data, offset)
        assert tag == b"CMP2" and 0 < stored_size <= raw_size <= 65536
        payload = data[offset+16:offset+16+stored_size]
        assert len(payload) == stored_size
        block = bytearray()
        if raw_size == stored_size:
            block.extend(payload)
        else:
            i = 0
            while i < len(payload):
                token = payload[i]; i += 1
                if token < 128:
                    block.extend(payload[i:i+token+1]); i += token+1
                else:
                    distance = struct.unpack_from("<H", payload, i)[0]; i += 2
                    assert 0 < distance <= len(block)
                    for _ in range((token & 127)+3):
                        block.append(block[-distance])
            assert i == len(payload)
        assert len(block) == raw_size and zlib.crc32(block) == checksum
        expanded.extend(block); offset += 16+stored_size
    assert offset == len(data)
    return bytes(expanded)

def main():
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="movie-api-", dir=ROOT / "_obj_") as temp:
        folder = Path(temp)
        (folder / "config.cfg").write_text("ApiServer=0\nResidentBootloader=0\nFrameRateLimiter=0\n", encoding="utf-8")
        # Infinite AVR loop; input/state validation is exercised by the C suite.
        header = bytearray(512); header[:6] = b"UZEBOX"; header[6] = 1
        header[8:12] = struct.pack("<I", 2)
        (folder / "game.uze").write_bytes(header + bytes([0xFF, 0xCF]))
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0)); port = probe.getsockname()[1]
        process = subprocess.Popen([str(exe), "--headless", "--paused", "--unthrottled", "--api-port", str(port), str(folder / "game.uze")],
            cwd=folder, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        client = CUzeBoxApi(port=port, timeout=20)
        try:
            client.wait_ready(20)
            def command(text):
                result = client.command(text)
                assert result.get("ok"), (text, result)
                return result
            assert command("INPUT_MOVIE STATUS")["recording_mode"] == "compact"
            command("INPUT_MOVIE MODE DEBUG")
            assert command("INPUT_MOVIE STATUS")["recording_mode"] == "debug"
            assert not client.command("INPUT_MOVIE MODE invalid").get("ok")
            movie = folder / "input with spaces.cic"
            command("INPUT_MOVIE RECORD RESET " + str(movie))
            command("RUN_FRAMES 256"); client.wait_run_complete(20)
            command("PAUSE"); command("INPUT_MOVIE STOP")
            expected = command("READ_REGS")
            data = movie.read_bytes()
            assert data[:8] == b"CZINPUT\0"
            offset = 16; frames = 0; state_chunk = None
            while offset < len(data):
                tag, size, checksum = struct.unpack_from("<4sII", data, offset)
                payload = data[offset+12:offset+12+size]
                assert len(payload) == size and zlib.crc32(payload) == checksum
                if tag == b"STAT":
                    state_chunk = offset
                frames += tag == b"FRAM"
                offset += 12 + size
            assert frames >= 256 and tag == b"END!" and offset == len(data)
            # Valid container CRC with an overflowing nested snapshot length
            # must be rejected before the machine state is touched.
            damaged = bytearray(data)
            stat_size = struct.unpack_from("<I", damaged, state_chunk + 4)[0]
            struct.pack_into("<I", damaged, state_chunk + 12 + 16, 0xFFFFFFF0)
            struct.pack_into("<I", damaged, state_chunk + 8,
                             zlib.crc32(damaged[state_chunk+12:state_chunk+12+stat_size]))
            (folder / "bad-state.cic").write_bytes(damaged)
            assert not client.command("INPUT_MOVIE PLAY " + str(folder / "bad-state.cic")).get("ok")
            assert command("READ_REGS") == expected
            oversized = bytearray(data); struct.pack_into("<I", oversized, 20, 0xFFFFFFFF)
            (folder / "oversized.cic").write_bytes(oversized)
            assert not client.command("INPUT_MOVIE PLAY " + str(folder / "oversized.cic")).get("ok")
            assert command("READ_REGS") == expected
            command("INPUT_MOVIE PLAY " + str(movie))
            command("RUN_FRAMES 100000")  # EOF must cancel an oversized run request.
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                status = command("INPUT_MOVIE STATUS")
                if not status["active"]:
                    break
                time.sleep(.01)
            assert not status["active"] and status["status"] == "Input replay complete.", status
            final = command("GET_STATE")
            assert final["paused"] and final["run_frames_remaining"] == 0
            assert command("READ_REGS") == expected
            command("SCREENSHOT_CAPTURE")
            # Earlier CZINPUT recordings remain valid with their .cap suffix.
            old_name = folder / "versioned.cap"
            old_name.write_bytes(data)
            command("INPUT_MOVIE PLAY " + str(old_name))
            command("PAUSE"); command("INPUT_MOVIE STOP")
            (folder / "legacy.cap").write_bytes(b"\0\0")
            rejected = client.command("INPUT_MOVIE PLAY " + str(folder / "legacy.cap"))
            assert not rejected.get("ok") and "legacy" in str(rejected).lower()
            # Repeat through the default compressed transport, independent of
            # the saved preference during playback. Check complete CPU state.
            command("INPUT_MOVIE MODE COMPACT")
            compact = folder / "compact.cic"
            command("INPUT_MOVIE RECORD RESET " + str(compact))
            command("RUN_FRAMES 256"); client.wait_run_complete(20)
            command("PAUSE"); command("INPUT_MOVIE STOP")
            compact_expected = command("READ_REGS")
            compact_data = compact.read_bytes()
            assert struct.unpack_from("<I", compact_data, 8)[0] == 4
            assert len(compact_data) < len(data)
            expanded = expand_compact(compact_data)
            # Replaying independently expanded v1 bytes proves identical semantics.
            (folder / "expanded.cic").write_bytes(expanded)
            command("INPUT_MOVIE MODE DEBUG")
            for broken in (raw_compact(expanded[16:-28]), raw_compact(expanded[16:] + b"x"),
                           compact_data[:-1], compact_data + b"x",
                           compact_data[:20] + struct.pack("<I", 65537) + compact_data[24:]):
                bad = folder / "bad-block.cic"; bad.write_bytes(broken)
                assert not client.command("INPUT_MOVIE PLAY " + str(bad)).get("ok")
                assert command("READ_REGS") == compact_expected
            command("INPUT_MOVIE PLAY " + str(compact))
            command("RUN_FRAMES 100000")
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                status = command("INPUT_MOVIE STATUS")
                if not status["active"]:
                    break
                time.sleep(.01)
            assert status["status"] == "Input replay complete.", status
            assert command("READ_REGS") == compact_expected
            assert status["recording_mode"] == "debug"
            command("INPUT_MOVIE PLAY " + str(folder / "expanded.cic"))
            command("RUN_FRAMES 100000"); client.wait_run_complete(20)
            assert command("INPUT_MOVIE STATUS")["status"] == "Input replay complete."
            assert command("READ_REGS") == compact_expected
            # Completed replays retain in-memory checkpoints. Seek rebuilds both
            # machine state and the partially decoded compressed block.
            def seek(frame):
                command(f"INPUT_MOVIE SEEK {frame}")
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    status=command("INPUT_MOVIE STATUS")
                    if command("RUN_STATUS")["paused"]:
                        status=command("INPUT_MOVIE STATUS")
                        assert status["frame"]==frame, status
                        return
                    time.sleep(.005)
                raise AssertionError("seek timed out")
            seek(40)
            command("INPUT_MOVIE CHECKPOINT TAKE 1")
            at40=command("READ_REGS")
            seek(80)
            command("INPUT_MOVIE CHECKPOINT RESTORE 1")
            assert command("READ_REGS")==at40
            seek(40); assert command("READ_REGS")==at40
            command("INPUT_MOVIE STOP")
            # Build a genuine old-format fixture: no behavior tag, old FRAM.
            legacy=bytearray(b"CZINPUT\0"+struct.pack("<II",1,16))
            off=16
            while off<len(data):
                tag,n,_=struct.unpack_from("<4sII",data,off)
                payload=bytearray(data[off+12:off+12+n])
                if tag==b"META": payload[72:80]=bytes(8)
                if tag==b"FRAM": payload=payload[:24]
                legacy+=struct.pack("<4sII",tag,len(payload),zlib.crc32(payload))+payload
                off+=12+n
            legacy_path=folder/"legacy-v1.cic"; legacy_path.write_bytes(legacy)
            command("INPUT_MOVIE PLAY "+str(legacy_path))
            command("RUN_FRAMES 100000"); client.wait_run_complete(20)
            assert command("INPUT_MOVIE STATUS")["status"]=="Input replay complete."
            # Different emulator behavior is rejected before restoring machine.
            changed=bytearray(data)
            struct.pack_into("<I",changed,28+72,0x12345678)
            struct.pack_into("<I",changed,24,zlib.crc32(changed[28:132]))
            bad=folder/"behavior.cic"; bad.write_bytes(changed)
            assert "behavior" in str(client.command("INPUT_MOVIE PLAY "+str(bad))).lower()
            # SPI-only fingerprint mismatch identifies the subsystem and frame.
            changed=bytearray(data); off=16
            while changed[off:off+4]!=b"FRAM": off+=12+struct.unpack_from("<I",changed,off+4)[0]
            n=struct.unpack_from("<I",changed,off+4)[0]
            changed[off+12+36]^=1
            struct.pack_into("<I",changed,off+8,zlib.crc32(changed[off+12:off+12+n]))
            bad=folder/"spi-mismatch.cic"; bad.write_bytes(changed)
            command("INPUT_MOVIE PLAY "+str(bad))
            deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                mismatch=command("INPUT_MOVIE STATUS")
                if not mismatch['active']: break
                time.sleep(.005)
            assert "SPIRAM expected" in mismatch["status"],mismatch
            print(f"256-frame API fixture: Full Debug {len(data)} bytes; Compact {len(compact_data)} bytes")
            command("QUIT"); process.wait(timeout=10)
            assert process.returncode == 0
        finally:
            client.close()
            if process.poll() is None:
                process.terminate(); process.wait(timeout=10)
    print("Input movie headless API: PASS")

if __name__ == "__main__":
    main()
